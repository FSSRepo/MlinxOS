#include "window.h"
#include "gl_wrapper.h"
#include <EGL/egl.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <cstdio>
#include <chrono>

namespace linweb {

struct Window::Impl {
    Display* dpy = nullptr;
    ::Window win = 0;
    EGLDisplay egl_dpy = EGL_NO_DISPLAY;
    EGLSurface egl_surf = EGL_NO_SURFACE;
    EGLContext egl_ctx = EGL_NO_CONTEXT;
    bool should_close = false;
    double cursor_x = 0;
    double cursor_y = 0;
    bool mouse_pressed = false;
    DeadKeys dead_keys;
};

// X11 keyboard modifier state -> KeyMod bits.
static int x11_mods(unsigned int state) {
    int mods = 0;
    if (state & ShiftMask) mods |= ModShift;
    if (state & LockMask) mods |= ModCapsLock;
    if (state & ControlMask) mods |= ModControl;
    if (state & Mod1Mask) mods |= ModAlt;
    if (state & Mod2Mask) mods |= ModNumLock;
    if (state & Mod4Mask) mods |= ModMeta;
    if (state & Mod5Mask) mods |= ModAltGraph;
    return mods;
}

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

    impl_->dpy = XOpenDisplay(NULL);
    if (!impl_->dpy) {
        printf("Cannot open X display\n");
        return false;
    }

    int screen = DefaultScreen(impl_->dpy);
    ::Window root = RootWindow(impl_->dpy, screen);

    width_ = DisplayWidth(impl_->dpy, screen);
    height_ = DisplayHeight(impl_->dpy, screen);

    XSetWindowAttributes swa;
    swa.override_redirect = False;
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | FocusChangeMask |
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                     StructureNotifyMask | EnterWindowMask;

    impl_->win = XCreateWindow(
        impl_->dpy, root,
        0, 0, width_, height_,
        0,
        CopyFromParent,
        InputOutput,
        CopyFromParent,
        CWEventMask,
        &swa);

    XMapWindow(impl_->dpy, impl_->win);
    // This kiosk session starts Xorg without a window manager. Request focus
    // explicitly so the keyboard is routed to Linweb as soon as it appears.
    XSetInputFocus(impl_->dpy, impl_->win, RevertToParent, CurrentTime);
    XStoreName(impl_->dpy, impl_->win, title_.c_str());

    Atom wm_delete_window = XInternAtom(impl_->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(impl_->dpy, impl_->win, &wm_delete_window, 1);

    Atom wm_state = XInternAtom(impl_->dpy, "_NET_WM_STATE", False);
    Atom wm_fullscr = XInternAtom(impl_->dpy, "_NET_WM_STATE_FULLSCREEN", False);

    XEvent xev{};
    xev.type = ClientMessage;
    xev.xclient.window = impl_->win;
    xev.xclient.message_type = wm_state;
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = 1;
    xev.xclient.data.l[1] = wm_fullscr;
    xev.xclient.data.l[2] = 0;

    XSendEvent(
        impl_->dpy,
        DefaultRootWindow(impl_->dpy),
        False,
        SubstructureRedirectMask | SubstructureNotifyMask,
        &xev);

    XFlush(impl_->dpy);

    /* EGL */
    impl_->egl_dpy = eglGetDisplay((EGLNativeDisplayType)impl_->dpy);
    if (impl_->egl_dpy == EGL_NO_DISPLAY) {
        printf("Cannot get EGL display\n");
        return false;
    }
    eglInitialize(impl_->egl_dpy, NULL, NULL);

    eglBindAPI(EGL_OPENGL_API);

    EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
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
        EGL_CONTEXT_MAJOR_VERSION, 2,
        EGL_CONTEXT_MINOR_VERSION, 0,
        EGL_NONE};

    impl_->egl_ctx = eglCreateContext(impl_->egl_dpy, cfg, EGL_NO_CONTEXT, ctx_attribs);
    if (impl_->egl_ctx == EGL_NO_CONTEXT) {
        printf("Cannot create EGL context\n");
        return false;
    }
    impl_->egl_surf = eglCreateWindowSurface(impl_->egl_dpy, cfg, (EGLNativeWindowType)impl_->win, NULL);
    eglMakeCurrent(impl_->egl_dpy, impl_->egl_surf, impl_->egl_surf, impl_->egl_ctx);

    if (!gladLoadGLLoader((GLADloadproc)eglGetProcAddress)) {
        printf("Failed to load OpenGL via EGL\n");
        return false;
    }

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
    if (impl_->win) {
        XDestroyWindow(impl_->dpy, impl_->win);
        impl_->win = 0;
    }
    if (impl_->dpy) {
        XCloseDisplay(impl_->dpy);
        impl_->dpy = nullptr;
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
    while (XPending(impl_->dpy)) {
        XEvent ev;
        XNextEvent(impl_->dpy, &ev);

        switch (ev.type) {
            case ClientMessage: {
                Atom wm_protocols = XInternAtom(impl_->dpy, "WM_PROTOCOLS", False);
                Atom wm_delete_window = XInternAtom(impl_->dpy, "WM_DELETE_WINDOW", False);
                if (ev.xclient.message_type == wm_protocols &&
                    (Atom)ev.xclient.data.l[0] == wm_delete_window) {
                    self->impl_->should_close = true;
                }
                break;
            }
            case MapNotify:
            case EnterNotify:
            case FocusOut:
                // No window manager: keep claiming the keyboard focus.
                XSetInputFocus(impl_->dpy, impl_->win, RevertToParent, CurrentTime);
                break;
            case DestroyNotify:
                self->impl_->should_close = true;
                break;
            case KeyPress:
            case KeyRelease: {
                Action action = (ev.type == KeyPress) ? Action::Press : Action::Release;
                // X11 auto-repeat arrives as a Release followed by a Press with the same time
                // and key: report the pair as a single Repeat.
                if (ev.type == KeyRelease && XPending(impl_->dpy)) {
                    XEvent next;
                    XPeekEvent(impl_->dpy, &next);
                    if (next.type == KeyPress && next.xkey.time == ev.xkey.time &&
                        next.xkey.keycode == ev.xkey.keycode) {
                        XNextEvent(impl_->dpy, &next);
                        action = Action::Repeat;
                    }
                }
                char text[32];
                KeySym keysym = NoSymbol;
                XLookupString(&ev.xkey, text, sizeof(text), &keysym, nullptr);   // applies shift, caps lock, AltGr
                KeySym base = XLookupKeysym(&ev.xkey, 0);                        // the key with no modifiers
                int mods = x11_mods(ev.xkey.state);
                KeyEvent key_ev = make_key_event(action, static_cast<int>(ev.xkey.keycode) - 8, mods,
                                                 static_cast<uint32_t>(keysym), static_cast<uint32_t>(base));
                if (action != Action::Release) self->impl_->dead_keys.process(static_cast<uint32_t>(keysym), key_ev);
                // Ctrl+Q closes the application (Escape is left to the page, for dialogs and "back").
                if (action == Action::Press && (mods & ModControl) && (keysym == XK_q || keysym == XK_Q)) {
                    self->impl_->should_close = true;
                }
                self->emit_key(key_ev);
                break;
            }
            case ButtonPress:
            case ButtonRelease: {
                int button = ev.xbutton.button;
                Action action = (ev.type == ButtonPress) ? Action::Press : Action::Release;
                if (button == 4 && ev.type == ButtonPress && self->get_scroll_callback()) {
                    self->get_scroll_callback()(self, 0.0, 1.0);
                } else if (button == 5 && ev.type == ButtonPress && self->get_scroll_callback()) {
                    self->get_scroll_callback()(self, 0.0, -1.0);
                } else {
                    MouseButton mb = (button == 1) ? MouseButton::Left :
                                     (button == 3) ? MouseButton::Right : MouseButton::Middle;
                    if (self->get_mouse_button_callback()) {
                        self->get_mouse_button_callback()(self, mb, action, 0);
                    }
                }
                if (button == 1) self->impl_->mouse_pressed = (ev.type == ButtonPress);
                break;
            }
            case MotionNotify: {
                self->impl_->cursor_x = ev.xmotion.x;
                self->impl_->cursor_y = ev.xmotion.y;
                if (self->get_cursor_pos_callback()) {
                    self->get_cursor_pos_callback()(self, self->impl_->cursor_x, self->impl_->cursor_y);
                }
                break;
            }
            case ConfigureNotify: {
                if (ev.xconfigure.width != width_ || ev.xconfigure.height != height_) {
                    self->width_ = ev.xconfigure.width;
                    self->height_ = ev.xconfigure.height;
                    glViewport(0, 0, self->width_, self->height_);
                }
                break;
            }
        }
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
    return reinterpret_cast<void*>(impl_->win);
}

double get_platform_time() {
    using namespace std::chrono;
    return duration<double>(high_resolution_clock::now().time_since_epoch()).count();
}

} // namespace linweb
