#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static struct wl_display *display = NULL;
static struct wl_compositor *compositor = NULL;
static struct xdg_wm_base *wm_base = NULL;
static struct wl_surface *surface = NULL;
static struct xdg_surface *xdg_surface = NULL;
static struct xdg_toplevel *toplevel = NULL;
static struct wl_egl_window *egl_window = NULL;

static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLConfig egl_config;
static EGLContext egl_context = EGL_NO_CONTEXT;
static EGLSurface egl_surface = EGL_NO_SURFACE;

static int running = 1;
static int configured = 0;
static uint32_t pending_width = 0;
static uint32_t pending_height = 0;
static uint32_t current_width = 640;
static uint32_t current_height = 480;

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    xdg_surface_ack_configure(xdg_surface, serial);
    configured = 1;

    // Apply the pending size (received in toplevel configure)
    if (pending_width > 0 && pending_height > 0) {
        current_width = pending_width;
        current_height = pending_height;
        printf("Resize to %ux%u\n", current_width, current_height);

        if (egl_window) {
            wl_egl_window_resize(egl_window, current_width, current_height, 0, 0);
        }
    }
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
                                   int32_t width, int32_t height,
                                   struct wl_array *states) {
    // width/height == 0 means "not suggested, use whatever you want"
    if (width > 0 && height > 0) {
        pending_width = (uint32_t)width;
        pending_height = (uint32_t)height;
    }
    // Here you could read states to check whether it is fullscreen, maximized, etc.
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    running = 0;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping,
};

static void registry_handler(void *data, struct wl_registry *registry,
                             uint32_t id, const char *interface, uint32_t version) {
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        wm_base = wl_registry_bind(registry, id, &xdg_wm_base_interface, 2);
        xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);
    }
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t id) {}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handler,
    .global_remove = registry_remove,
};

int main(void) {
    display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Could not connect to Wayland display\n");
        return EXIT_FAILURE;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    if (!compositor || !wm_base) {
        fprintf(stderr, "Required interfaces not found\n");
        return EXIT_FAILURE;
    }

    surface = wl_compositor_create_surface(compositor);
    xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);

    toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_add_listener(toplevel, &xdg_toplevel_listener, NULL);

    // Optional: title and app_id so the compositor displays it properly
    xdg_toplevel_set_title(toplevel, "Wayland Test");
    xdg_toplevel_set_app_id(toplevel, "egl-test");

    // Fullscreen (the compositor will give us the monitor size)
    xdg_toplevel_set_fullscreen(toplevel, NULL);

    wl_surface_commit(surface);

    // Wait for the first configure
    while (!configured) {
        wl_display_dispatch(display);
    }

    // Create the EGL window with initial size (it will be adjusted later)
    egl_window = wl_egl_window_create(surface, current_width, current_height);
    if (!egl_window) {
        fprintf(stderr, "Could not create wl_egl_window\n");
        return EXIT_FAILURE;
    }

    egl_display = eglGetDisplay((EGLNativeDisplayType)display);
    if (egl_display == EGL_NO_DISPLAY || !eglInitialize(egl_display, NULL, NULL)) {
        fprintf(stderr, "EGL init failed\n");
        return EXIT_FAILURE;
    }

    EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE,   8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8,
        EGL_ALPHA_SIZE, 0,
        EGL_NONE
    };

    EGLint num_configs;
    if (!eglChooseConfig(egl_display, attribs, &egl_config, 1, &num_configs) || num_configs == 0) {
        fprintf(stderr, "EGL config not found\n");
        return EXIT_FAILURE;
    }

    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, ctx_attr);
    if (egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Could not create GLES2 context\n");
        return EXIT_FAILURE;
    }

    egl_surface = eglCreateWindowSurface(egl_display, egl_config,
                                         (EGLNativeWindowType)egl_window, NULL);
    if (egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "Could not create EGL surface\n");
        return EXIT_FAILURE;
    }

    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        fprintf(stderr, "eglMakeCurrent failed\n");
        return EXIT_FAILURE;
    }

    // Main loop
    while (running) {
        // Clear pending events
        while (wl_display_dispatch_pending(display) > 0) {
            // If there was a resize -> wl_egl_window_resize was already called in configure
        }

        glViewport(0, 0, current_width, current_height);
        glClearColor(0.0f, 1.0f, 0.0f, 1.0f); // green
        glClear(GL_COLOR_BUFFER_BIT);

        eglSwapBuffers(egl_display, egl_surface);

        usleep(16000); // ~60 fps
    }

    // Cleanup
    eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl_display, egl_surface);
    eglDestroyContext(egl_display, egl_context);
    eglTerminate(egl_display);

    wl_egl_window_destroy(egl_window);
    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);
    wl_display_disconnect(display);

    return EXIT_SUCCESS;
}