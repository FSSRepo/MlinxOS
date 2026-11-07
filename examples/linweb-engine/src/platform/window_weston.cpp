#include "window.h"
#include "gl_wrapper.h"
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <xkbcommon/xkbcommon.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <chrono>

namespace linweb {

struct Window::Impl {
    wl_display* display = nullptr;
    wl_compositor* compositor = nullptr;
    xdg_wm_base* wm_base = nullptr;
    wl_surface* wl_surf = nullptr;
    xdg_surface* xdg_surf = nullptr;
    xdg_toplevel* xdg_top = nullptr;
    wl_seat* seat = nullptr;
    wl_keyboard* keyboard = nullptr;
    wl_egl_window* egl_window = nullptr;

    EGLDisplay egl_dpy = EGL_NO_DISPLAY;
    EGLSurface egl_surf = EGL_NO_SURFACE;
    EGLContext egl_ctx = EGL_NO_CONTEXT;

    bool should_close = false;
    bool configured = false;
    uint32_t pending_width = 0;
    uint32_t pending_height = 0;
    uint32_t current_width = 0;
    uint32_t current_height = 0;
    double cursor_x = 0;
    double cursor_y = 0;
    bool mouse_pressed = false;
    Window* owner = nullptr;

    xkb_context* xkb_ctx = nullptr;
    xkb_keymap* xkb_map = nullptr;
    xkb_state* xkb_st = nullptr;
    DeadKeys dead_keys;
    int32_t repeat_rate = 25;        // keys per second; wl_keyboard.repeat_info overrides it
    int32_t repeat_delay = 600;      // ms before the first repeat
    uint32_t repeat_key = 0;         // evdev code of the key being held (0 = none)
    std::chrono::steady_clock::time_point next_repeat;
};

// Keyboard: the compositor sends its xkb keymap; xkbcommon turns evdev key codes into
// symbols with the current modifiers (shift, caps lock, AltGr), so every key of the
// layout works. Key repeat is done here because Wayland leaves it to the client.
static void keyboard_keymap(void* data, struct wl_keyboard*, uint32_t format, int fd, uint32_t size) {
    auto* impl = static_cast<Window::Impl*>(data);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }
    void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return;
    if (!impl->xkb_ctx) impl->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_keymap* keymap = xkb_keymap_new_from_string(impl->xkb_ctx, static_cast<const char*>(map),
                                                    XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    if (!keymap) return;
    xkb_state* state = xkb_state_new(keymap);
    if (impl->xkb_st) xkb_state_unref(impl->xkb_st);
    if (impl->xkb_map) xkb_keymap_unref(impl->xkb_map);
    impl->xkb_map = keymap;
    impl->xkb_st = state;
    impl->repeat_key = 0;
}

static void keyboard_enter(void*, struct wl_keyboard*, uint32_t, struct wl_surface*, struct wl_array*) {}

static void keyboard_leave(void* data, struct wl_keyboard*, uint32_t, struct wl_surface*) {
    auto* impl = static_cast<Window::Impl*>(data);
    impl->repeat_key = 0;
    impl->dead_keys.reset();
}

static void keyboard_modifiers(void* data, struct wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched,
                               uint32_t locked, uint32_t group) {
    auto* impl = static_cast<Window::Impl*>(data);
    if (impl->xkb_st) xkb_state_update_mask(impl->xkb_st, depressed, latched, locked, 0, 0, group);
}

static void keyboard_repeat_info(void* data, struct wl_keyboard*, int32_t rate, int32_t delay) {
    auto* impl = static_cast<Window::Impl*>(data);
    impl->repeat_rate = rate;
    impl->repeat_delay = delay;
}

static int current_mods(Window::Impl* impl) {
    auto active = [&](const char* name) {
        return xkb_state_mod_name_is_active(impl->xkb_st, name, XKB_STATE_MODS_EFFECTIVE) > 0;
    };
    int mods = 0;
    if (active(XKB_MOD_NAME_SHIFT)) mods |= ModShift;
    if (active(XKB_MOD_NAME_CTRL)) mods |= ModControl;
    if (active(XKB_MOD_NAME_ALT)) mods |= ModAlt;
    if (active(XKB_MOD_NAME_LOGO)) mods |= ModMeta;
    if (active(XKB_MOD_NAME_CAPS)) mods |= ModCapsLock;
    if (active(XKB_MOD_NAME_NUM)) mods |= ModNumLock;
    if (active("Mod5")) mods |= ModAltGraph;
    return mods;
}

// Builds the event for an evdev key code and delivers it.
static void deliver_key(Window::Impl* impl, uint32_t evdev, Action action) {
    if (!impl->xkb_st || !impl->owner) return;
    xkb_keycode_t keycode = evdev + 8;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(impl->xkb_st, keycode);
    xkb_keysym_t base = sym;
    xkb_layout_index_t layout = xkb_state_key_get_layout(impl->xkb_st, keycode);
    const xkb_keysym_t* syms = nullptr;
    if (xkb_keymap_key_get_syms_by_level(impl->xkb_map, keycode, layout, 0, &syms) > 0 && syms) base = syms[0];
    KeyEvent ev = make_key_event(action, static_cast<int>(evdev), current_mods(impl), sym, base);
    if (action != Action::Release) impl->dead_keys.process(sym, ev);
    // Ctrl+Q closes the application (Escape is left to the page, for dialogs and "back").
    if (action == Action::Press && (ev.mods & ModControl) && (sym == 'q' || sym == 'Q')) impl->should_close = true;
    impl->owner->emit_key(ev);
}

static void keyboard_key(void* data, struct wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
    auto* impl = static_cast<Window::Impl*>(data);
    if (!impl->xkb_st) return;
    bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
    deliver_key(impl, key, pressed ? Action::Press : Action::Release);
    if (pressed) {
        if (impl->repeat_rate > 0 && xkb_keymap_key_repeats(impl->xkb_map, key + 8)) {
            impl->repeat_key = key;
            impl->next_repeat = std::chrono::steady_clock::now() + std::chrono::milliseconds(impl->repeat_delay);
        }
    } else if (impl->repeat_key == key) {
        impl->repeat_key = 0;
    }
}

// Called every frame: sends the Repeat events of a key that is being held down.
static void keyboard_repeat_tick(Window::Impl* impl) {
    if (!impl->repeat_key || impl->repeat_rate <= 0) return;
    auto now = std::chrono::steady_clock::now();
    auto period = std::chrono::milliseconds(1000 / impl->repeat_rate);
    for (int i = 0; i < 4 && impl->repeat_key && now >= impl->next_repeat; ++i) {
        impl->next_repeat += period;
        deliver_key(impl, impl->repeat_key, Action::Repeat);
    }
    if (now >= impl->next_repeat) impl->next_repeat = now + period;   // fell behind: do not burst
}

static const struct wl_keyboard_listener keyboard_listener = {
    keyboard_keymap,
    keyboard_enter,
    keyboard_leave,
    keyboard_key,
    keyboard_modifiers,
    keyboard_repeat_info,
};

static void seat_capabilities(void* data, struct wl_seat* seat, uint32_t capabilities) {
    auto* impl = static_cast<Window::Impl*>(data);
    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        impl->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(impl->keyboard, &keyboard_listener, impl);
    }
}

static void seat_name(void*, struct wl_seat*, const char*) {}

static const struct wl_seat_listener seat_listener = {
    seat_capabilities,
    seat_name,
};

static void xdg_surface_configure(void* data, struct xdg_surface* xdg_surface, uint32_t serial) {
    auto* impl = static_cast<Window::Impl*>(data);
    xdg_surface_ack_configure(xdg_surface, serial);
    impl->configured = true;
    if (impl->pending_width > 0 && impl->pending_height > 0) {
        impl->current_width = impl->pending_width;
        impl->current_height = impl->pending_height;
        if (impl->egl_window) {
            wl_egl_window_resize(impl->egl_window, impl->current_width, impl->current_height, 0, 0);
        }
    }
}

static const struct xdg_surface_listener xdg_surface_listener = {
    xdg_surface_configure,
};

static void xdg_toplevel_configure(void* data, struct xdg_toplevel*, int32_t width, int32_t height, struct wl_array*) {
    auto* impl = static_cast<Window::Impl*>(data);
    if (width > 0 && height > 0) {
        impl->pending_width = static_cast<uint32_t>(width);
        impl->pending_height = static_cast<uint32_t>(height);
    }
}

static void xdg_toplevel_close(void* data, struct xdg_toplevel*) {
    auto* impl = static_cast<Window::Impl*>(data);
    impl->should_close = true;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    xdg_toplevel_configure,
    xdg_toplevel_close,
};

static void wm_base_ping(void*, struct xdg_wm_base* wm_base, uint32_t serial) {
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    wm_base_ping,
};

static void registry_handler(void* data, struct wl_registry* registry, uint32_t id, const char* interface, uint32_t version) {
    auto* impl = static_cast<Window::Impl*>(data);
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        impl->compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, id, &wl_compositor_interface, 4));
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        impl->wm_base = static_cast<xdg_wm_base*>(wl_registry_bind(registry, id, &xdg_wm_base_interface, 2));
        xdg_wm_base_add_listener(impl->wm_base, &wm_base_listener, nullptr);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        impl->seat = static_cast<wl_seat*>(wl_registry_bind(registry, id, &wl_seat_interface, 5));
        wl_seat_add_listener(impl->seat, &seat_listener, impl);
    }
}

static void registry_remove(void*, struct wl_registry*, uint32_t) {}

static const struct wl_registry_listener registry_listener = {
    registry_handler,
    registry_remove,
};

Window::Window() : impl_(new Impl()), width_(800), height_(600), title_("Linweb"),
    scroll_cb_(nullptr), key_cb_(nullptr), mouse_btn_cb_(nullptr), cursor_pos_cb_(nullptr) {}

Window::~Window() {
    shutdown();
    delete impl_;
}

bool Window::init(int width, int height, const std::string& title) {
    (void)width;
    (void)height;
    title_ = title;
    impl_->owner = this;

    impl_->display = wl_display_connect(nullptr);
    if (!impl_->display) {
        printf("Cannot connect to Wayland display\n");
        return false;
    }

    struct wl_registry* registry = wl_display_get_registry(impl_->display);
    wl_registry_add_listener(registry, &registry_listener, impl_);
    wl_display_roundtrip(impl_->display);

    if (!impl_->compositor || !impl_->wm_base) {
        printf("Missing required Wayland interfaces\n");
        return false;
    }

    impl_->wl_surf = wl_compositor_create_surface(impl_->compositor);
    impl_->xdg_surf = xdg_wm_base_get_xdg_surface(impl_->wm_base, impl_->wl_surf);
    xdg_surface_add_listener(impl_->xdg_surf, &xdg_surface_listener, impl_);

    impl_->xdg_top = xdg_surface_get_toplevel(impl_->xdg_surf);
    xdg_toplevel_add_listener(impl_->xdg_top, &xdg_toplevel_listener, impl_);
    xdg_toplevel_set_title(impl_->xdg_top, title_.c_str());
    xdg_toplevel_set_app_id(impl_->xdg_top, "linweb");
    xdg_toplevel_set_fullscreen(impl_->xdg_top, nullptr);

    wl_surface_commit(impl_->wl_surf);

    while (!impl_->configured) {
        wl_display_dispatch(impl_->display);
    }

    if (impl_->current_width == 0 || impl_->current_height == 0) {
        impl_->current_width = 640;
        impl_->current_height = 480;
    }

    width_ = static_cast<int>(impl_->current_width);
    height_ = static_cast<int>(impl_->current_height);

    impl_->egl_window = wl_egl_window_create(impl_->wl_surf, width_, height_);
    if (!impl_->egl_window) {
        printf("Failed to create wl_egl_window\n");
        return false;
    }

    impl_->egl_dpy = eglGetDisplay((EGLNativeDisplayType)impl_->display);
    if (impl_->egl_dpy == EGL_NO_DISPLAY) {
        printf("Cannot get EGL display\n");
        return false;
    }
    eglInitialize(impl_->egl_dpy, nullptr, nullptr);
    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE};

    EGLConfig cfg;
    EGLint num;
    if (!eglChooseConfig(impl_->egl_dpy, attribs, &cfg, 1, &num) || num < 1) {
        printf("No suitable EGL config found\n");
        return false;
    }

    EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE};

    impl_->egl_ctx = eglCreateContext(impl_->egl_dpy, cfg, EGL_NO_CONTEXT, ctx_attribs);
    if (impl_->egl_ctx == EGL_NO_CONTEXT) {
        printf("Cannot create EGL context\n");
        return false;
    }

    impl_->egl_surf = eglCreateWindowSurface(impl_->egl_dpy, cfg, (EGLNativeWindowType)impl_->egl_window, nullptr);
    if (impl_->egl_surf == EGL_NO_SURFACE) {
        printf("Cannot create EGL surface\n");
        return false;
    }

    eglMakeCurrent(impl_->egl_dpy, impl_->egl_surf, impl_->egl_surf, impl_->egl_ctx);

    glViewport(0, 0, width_, height_);
    return true;
}

void Window::shutdown() {
    if (impl_->egl_dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(impl_->egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (impl_->egl_surf != EGL_NO_SURFACE) eglDestroySurface(impl_->egl_dpy, impl_->egl_surf);
        if (impl_->egl_ctx != EGL_NO_CONTEXT) eglDestroyContext(impl_->egl_dpy, impl_->egl_ctx);
        eglTerminate(impl_->egl_dpy);
        impl_->egl_dpy = EGL_NO_DISPLAY;
        impl_->egl_surf = EGL_NO_SURFACE;
        impl_->egl_ctx = EGL_NO_CONTEXT;
    }
    if (impl_->egl_window) {
        wl_egl_window_destroy(impl_->egl_window);
        impl_->egl_window = nullptr;
    }
    if (impl_->keyboard) {
        wl_keyboard_destroy(impl_->keyboard);
        impl_->keyboard = nullptr;
    }
    if (impl_->xkb_st) { xkb_state_unref(impl_->xkb_st); impl_->xkb_st = nullptr; }
    if (impl_->xkb_map) { xkb_keymap_unref(impl_->xkb_map); impl_->xkb_map = nullptr; }
    if (impl_->xkb_ctx) { xkb_context_unref(impl_->xkb_ctx); impl_->xkb_ctx = nullptr; }
    if (impl_->seat) {
        wl_seat_destroy(impl_->seat);
        impl_->seat = nullptr;
    }
    if (impl_->xdg_top) {
        xdg_toplevel_destroy(impl_->xdg_top);
        impl_->xdg_top = nullptr;
    }
    if (impl_->xdg_surf) {
        xdg_surface_destroy(impl_->xdg_surf);
        impl_->xdg_surf = nullptr;
    }
    if (impl_->wl_surf) {
        wl_surface_destroy(impl_->wl_surf);
        impl_->wl_surf = nullptr;
    }
    if (impl_->display) {
        wl_display_disconnect(impl_->display);
        impl_->display = nullptr;
    }
}

bool Window::should_close() const {
    return impl_->should_close;
}

void Window::swap_buffers() const {
    eglSwapBuffers(impl_->egl_dpy, impl_->egl_surf);
}

void Window::poll_events() const {
    Window* self = const_cast<Window*>(this);
    wl_display_dispatch_pending(impl_->display);
    keyboard_repeat_tick(impl_);
    wl_display_flush(impl_->display);
    if (impl_->current_width != static_cast<uint32_t>(width_) ||
        impl_->current_height != static_cast<uint32_t>(height_)) {
        self->width_ = static_cast<int>(impl_->current_width);
        self->height_ = static_cast<int>(impl_->current_height);
        glViewport(0, 0, self->width_, self->height_);
    }
}

void Window::get_framebuffer_size(int& width, int& height) const {
    width = width_;
    height = height_;
}

void Window::get_window_size(int& width, int& height) const {
    width = width_;
    height = height_;
}

void Window::get_cursor_pos(double& x, double& y) const {
    x = impl_->cursor_x;
    y = impl_->cursor_y;
}

bool Window::is_mouse_button_pressed(MouseButton button) const {
    return (button == MouseButton::Left) && impl_->mouse_pressed;
}

double Window::get_time() const {
    using namespace std::chrono;
    return duration<double>(high_resolution_clock::now().time_since_epoch()).count();
}

void Window::set_scroll_callback(ScrollCallback callback) {
    scroll_cb_ = callback;
}

void Window::set_key_callback(KeyCallback callback) {
    key_cb_ = callback;
}

void Window::set_mouse_button_callback(MouseButtonCallback callback) {
    mouse_btn_cb_ = callback;
}

void Window::set_cursor_pos_callback(CursorPosCallback callback) {
    cursor_pos_cb_ = callback;
}

void Window::make_context_current() {
    eglMakeCurrent(impl_->egl_dpy, impl_->egl_surf, impl_->egl_surf, impl_->egl_ctx);
}

void Window::enable_blend() {
    glEnable(GL_BLEND);
}

void* Window::get_native_window() const {
    return reinterpret_cast<void*>(impl_->wl_surf);
}

double get_platform_time() {
    using namespace std::chrono;
    return duration<double>(high_resolution_clock::now().time_since_epoch()).count();
}

} // namespace linweb
