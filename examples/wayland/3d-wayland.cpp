#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

#include <wayland-egl.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <linux/input-event-codes.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctime>
#include <string.h>
#include <math.h>

#include "ogl_math.h"
#include "camera.h"

/*
wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml libs/xdg-shell-client-protocol.h
wayland-scanner private-code  /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml libs/xdg-shell-client-protocol.c

gcc -c libs/xdg-shell-client-protocol.c -o bin/xdg-shell-client-protocol.o -I.

g++ 3d-wayland.cpp \
    bin/xdg-shell-client-protocol.o \
    -o bin/cube \
    -lwayland-client \
    -lwayland-egl \
    -lEGL \
    -lGLESv2 \
    -Ilibs
*/

/* ========================================================= */
/* ====================== WAYLAND ========================== */
/* ========================================================= */

static struct wl_display *display = NULL;
static struct wl_compositor *compositor = NULL;
static struct xdg_wm_base *wm_base = NULL;

static struct wl_surface *surface = NULL;
static struct xdg_surface *xdg_surface = NULL;
static struct xdg_toplevel *toplevel = NULL;

static struct wl_egl_window *egl_window = NULL;

static struct wl_seat *seat = NULL;
static struct wl_keyboard *keyboard = NULL;

/* ========================================================= */
/* ========================= EGL =========================== */
/* ========================================================= */

static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLConfig egl_config;
static EGLContext egl_context = EGL_NO_CONTEXT;
static EGLSurface egl_surface = EGL_NO_SURFACE;

/* ========================================================= */

static int running = 1;
static int configured = 0;

static uint32_t pending_width = 0;
static uint32_t pending_height = 0;

static uint32_t current_width = 640;
static uint32_t current_height = 480;

/* ========================================================= */
/* ======================== SHADERS ======================== */
/* ========================================================= */

const char *vertexShaderSource =
    "attribute vec3 aPos;                \n"
    "attribute vec3 aColor;              \n"
    "varying vec3 vColor;                \n"
    "uniform mat4 model;                 \n"
    "uniform mat4 view;                  \n"
    "uniform mat4 projection;            \n"
    "void main() {                       \n"
    "    gl_Position = projection * view * model * vec4(aPos, 1.0);\n"
    "    vColor = aColor;                \n"
    "}";

const char *fragmentShaderSource =
    "precision mediump float;            \n"
    "varying vec3 vColor;                \n"
    "void main() {                       \n"
    "    gl_FragColor = vec4(vColor, 1.0);\n"
    "}";

/* ========================================================= */
/* ==================== SHADER UTILS ======================= */
/* ========================================================= */

GLuint compileShader(GLenum type, const char *src)
{
    GLuint shader = glCreateShader(type);

    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

    if (!ok)
    {
        char log[512];

        glGetShaderInfoLog(shader, 512, NULL, log);

        printf("Shader error:\n%s\n", log);

        exit(1);
    }

    return shader;
}

GLuint createProgram()
{
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    GLuint program = glCreateProgram();

    glAttachShader(program, vs);
    glAttachShader(program, fs);

    glLinkProgram(program);

    GLint ok;

    glGetProgramiv(program, GL_LINK_STATUS, &ok);

    if (!ok)
    {
        char log[512];

        glGetProgramInfoLog(program, 512, NULL, log);

        printf("Program link error:\n%s\n", log);

        exit(1);
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    return program;
}

/* ========================================================= */
/* ==================== KEYBOARD EVENTS ==================== */
/* ========================================================= */

static void keyboard_keymap(void *data,
                            struct wl_keyboard *wl_keyboard,
                            uint32_t format,
                            int fd,
                            uint32_t size)
{
    close(fd);
}

static void keyboard_enter(void *data,
                           struct wl_keyboard *wl_keyboard,
                           uint32_t serial,
                           struct wl_surface *surface,
                           struct wl_array *keys)
{
}

static void keyboard_leave(void *data,
                           struct wl_keyboard *wl_keyboard,
                           uint32_t serial,
                           struct wl_surface *surface)
{
}

static void keyboard_key(void *data,
                         struct wl_keyboard *wl_keyboard,
                         uint32_t serial,
                         uint32_t time,
                         uint32_t key,
                         uint32_t state)
{
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
    {
        if (key == KEY_ESC)
        {
            printf("ESC pressed\n");
            running = 0;
        }
    }
}

static void keyboard_modifiers(void *data,
                               struct wl_keyboard *wl_keyboard,
                               uint32_t serial,
                               uint32_t mods_depressed,
                               uint32_t mods_latched,
                               uint32_t mods_locked,
                               uint32_t group)
{
}

static void keyboard_repeat_info(void *data,
                                 struct wl_keyboard *wl_keyboard,
                                 int32_t rate,
                                 int32_t delay)
{
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

/* ========================================================= */
/* ====================== SEAT EVENTS ====================== */
/* ========================================================= */

static void seat_capabilities(void *data,
                              struct wl_seat *seat,
                              uint32_t capabilities)
{
    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD)
    {
        keyboard = wl_seat_get_keyboard(seat);

        wl_keyboard_add_listener(
            keyboard,
            &keyboard_listener,
            NULL);
    }
}

static void seat_name(void *data,
                      struct wl_seat *seat,
                      const char *name)
{
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

/* ========================================================= */
/* ===================== XDG EVENTS ======================== */
/* ========================================================= */

static void xdg_surface_configure(void *data,
                                  struct xdg_surface *xdg_surface,
                                  uint32_t serial)
{
    xdg_surface_ack_configure(xdg_surface, serial);

    configured = 1;

    if (pending_width > 0 && pending_height > 0)
    {
        current_width = pending_width;
        current_height = pending_height;

        printf("Resize: %ux%u\n",
               current_width,
               current_height);

        if (egl_window)
        {
            wl_egl_window_resize(
                egl_window,
                current_width,
                current_height,
                0,
                0);
        }

        glViewport(0, 0,
                   current_width,
                   current_height);
    }
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data,
                                   struct xdg_toplevel *xdg_toplevel,
                                   int32_t width,
                                   int32_t height,
                                   struct wl_array *states)
{
    if (width > 0 && height > 0)
    {
        pending_width = (uint32_t)width;
        pending_height = (uint32_t)height;
    }
}

static void xdg_toplevel_close(void *data,
                               struct xdg_toplevel *xdg_toplevel)
{
    running = 0;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

static void wm_base_ping(void *data,
                         struct xdg_wm_base *wm_base,
                         uint32_t serial)
{
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping,
};

/* ========================================================= */
/* ==================== REGISTRY EVENTS ==================== */
/* ========================================================= */

static void registry_handler(void *data,
                             struct wl_registry *registry,
                             uint32_t id,
                             const char *interface,
                             uint32_t version)
{
    if (strcmp(interface, wl_compositor_interface.name) == 0)
    {
        compositor = (wl_compositor *)wl_registry_bind(
            registry,
            id,
            &wl_compositor_interface,
            4);
    }
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
    {
        wm_base = (xdg_wm_base *)wl_registry_bind(
            registry,
            id,
            &xdg_wm_base_interface,
            2);

        xdg_wm_base_add_listener(
            wm_base,
            &wm_base_listener,
            NULL);
    }
    else if (strcmp(interface, wl_seat_interface.name) == 0)
    {
        seat = (wl_seat *)wl_registry_bind(
            registry,
            id,
            &wl_seat_interface,
            5);

        wl_seat_add_listener(
            seat,
            &seat_listener,
            NULL);
    }
}

static void registry_remove(void *data,
                            struct wl_registry *registry,
                            uint32_t id)
{
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handler,
    .global_remove = registry_remove,
};

/* ========================================================= */
/* =========================== MAIN ======================== */
/* ========================================================= */

int main(void)
{
    display = wl_display_connect(NULL);

    if (!display)
    {
        fprintf(stderr, "Could not connect to Wayland\n");
        return EXIT_FAILURE;
    }

    struct wl_registry *registry =
        wl_display_get_registry(display);

    wl_registry_add_listener(
        registry,
        &registry_listener,
        NULL);

    wl_display_roundtrip(display);

    if (!compositor || !wm_base)
    {
        fprintf(stderr,
                "Missing Wayland interfaces\n");

        return EXIT_FAILURE;
    }

    /* ================= SURFACE ================= */

    surface =
        wl_compositor_create_surface(compositor);

    xdg_surface =
        xdg_wm_base_get_xdg_surface(
            wm_base,
            surface);

    xdg_surface_add_listener(
        xdg_surface,
        &xdg_surface_listener,
        NULL);

    toplevel =
        xdg_surface_get_toplevel(xdg_surface);

    xdg_toplevel_add_listener(
        toplevel,
        &xdg_toplevel_listener,
        NULL);

    xdg_toplevel_set_title(
        toplevel,
        "Wayland OpenGL ES2 Cube");

    xdg_toplevel_set_app_id(
        toplevel,
        "cube-demo");

    xdg_toplevel_set_fullscreen(
        toplevel,
        NULL);

    wl_surface_commit(surface);

    /* Wait for the initial configure */

    while (!configured)
    {
        wl_display_dispatch(display);
    }

    /* ================= EGL ================= */

    egl_window =
        wl_egl_window_create(
            surface,
            current_width,
            current_height);

    if (!egl_window)
    {
        fprintf(stderr,
                "Could not create wl_egl_window\n");

        return EXIT_FAILURE;
    }

    egl_display =
        eglGetDisplay(
            (EGLNativeDisplayType)display);

    if (egl_display == EGL_NO_DISPLAY)
    {
        fprintf(stderr,
                "eglGetDisplay failed\n");

        return EXIT_FAILURE;
    }

    if (!eglInitialize(egl_display, NULL, NULL))
    {
        fprintf(stderr,
                "eglInitialize failed\n");

        return EXIT_FAILURE;
    }

    EGLint attribs[] = {
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES2_BIT,

        EGL_SURFACE_TYPE,
        EGL_WINDOW_BIT,

        EGL_RED_SIZE,
        8,

        EGL_GREEN_SIZE,
        8,

        EGL_BLUE_SIZE,
        8,

        EGL_DEPTH_SIZE,
        24,

        EGL_NONE};

    EGLint num_configs;

    if (!eglChooseConfig(
            egl_display,
            attribs,
            &egl_config,
            1,
            &num_configs) ||
        num_configs == 0)
    {
        fprintf(stderr,
                "EGL config not found\n");

        return EXIT_FAILURE;
    }

    EGLint ctx_attr[] = {
        EGL_CONTEXT_CLIENT_VERSION,
        2,
        EGL_NONE};

    egl_context =
        eglCreateContext(
            egl_display,
            egl_config,
            EGL_NO_CONTEXT,
            ctx_attr);

    if (egl_context == EGL_NO_CONTEXT)
    {
        fprintf(stderr,
                "Could not create GLES2 context\n");

        return EXIT_FAILURE;
    }

    egl_surface =
        eglCreateWindowSurface(
            egl_display,
            egl_config,
            (EGLNativeWindowType)egl_window,
            NULL);

    if (egl_surface == EGL_NO_SURFACE)
    {
        fprintf(stderr,
                "Could not create EGLSurface\n");

        return EXIT_FAILURE;
    }

    if (!eglMakeCurrent(
            egl_display,
            egl_surface,
            egl_surface,
            egl_context))
    {
        fprintf(stderr,
                "eglMakeCurrent failed\n");

        return EXIT_FAILURE;
    }

    /* ================= OPENGL ================= */

    glViewport(
        0,
        0,
        current_width,
        current_height);

    glEnable(GL_DEPTH_TEST);

    GLuint program = createProgram();

    glUseProgram(program);

    /* ================= CUBE ================= */

    float vertices[] = {
        // Front
        -0.5f, -0.5f, 0.5f, 1, 0, 0,
        0.5f, -0.5f, 0.5f, 1, 0, 0,
        0.5f, 0.5f, 0.5f, 1, 0, 0,

        0.5f, 0.5f, 0.5f, 1, 0, 0,
        -0.5f, 0.5f, 0.5f, 1, 0, 0,
        -0.5f, -0.5f, 0.5f, 1, 0, 0,

        // Back
        -0.5f, -0.5f, -0.5f, 0, 1, 0,
        0.5f, -0.5f, -0.5f, 0, 1, 0,
        0.5f, 0.5f, -0.5f, 0, 1, 0,

        0.5f, 0.5f, -0.5f, 0, 1, 0,
        -0.5f, 0.5f, -0.5f, 0, 1, 0,
        -0.5f, -0.5f, -0.5f, 0, 1, 0,

        // Top
        -0.5f, 0.5f, 0.5f, 0, 0, 1,
        -0.5f, 0.5f, -0.5f, 0, 0, 1,
        0.5f, 0.5f, -0.5f, 0, 0, 1,

        0.5f, 0.5f, -0.5f, 0, 0, 1,
        0.5f, 0.5f, 0.5f, 0, 0, 1,
        -0.5f, 0.5f, 0.5f, 0, 0, 1,

        // Bottom
        -0.5f, -0.5f, 0.5f, 1, 1, 0,
        -0.5f, -0.5f, -0.5f, 1, 1, 0,
        0.5f, -0.5f, -0.5f, 1, 1, 0,

        0.5f, -0.5f, -0.5f, 1, 1, 0,
        0.5f, -0.5f, 0.5f, 1, 1, 0,
        -0.5f, -0.5f, 0.5f, 1, 1, 0,

        // Right
        0.5f, -0.5f, 0.5f, 1, 0, 1,
        0.5f, -0.5f, -0.5f, 1, 0, 1,
        0.5f, 0.5f, -0.5f, 1, 0, 1,

        0.5f, 0.5f, -0.5f, 1, 0, 1,
        0.5f, 0.5f, 0.5f, 1, 0, 1,
        0.5f, -0.5f, 0.5f, 1, 0, 1,

        // Left
        -0.5f, -0.5f, 0.5f, 0, 1, 1,
        -0.5f, -0.5f, -0.5f, 0, 1, 1,
        -0.5f, 0.5f, -0.5f, 0, 1, 1,

        -0.5f, 0.5f, -0.5f, 0, 1, 1,
        -0.5f, 0.5f, 0.5f, 0, 1, 1,
        -0.5f, -0.5f, 0.5f, 0, 1, 1,
    };

    GLuint vbo;

    glGenBuffers(1, &vbo);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(vertices),
        vertices,
        GL_STATIC_DRAW);

    GLint posLoc =
        glGetAttribLocation(program, "aPos");

    GLint colLoc =
        glGetAttribLocation(program, "aColor");

    glVertexAttribPointer(
        posLoc,
        3,
        GL_FLOAT,
        GL_FALSE,
        6 * sizeof(float),
        (void *)0);

    glEnableVertexAttribArray(posLoc);

    glVertexAttribPointer(
        colLoc,
        3,
        GL_FLOAT,
        GL_FALSE,
        6 * sizeof(float),
        (void *)(3 * sizeof(float)));

    glEnableVertexAttribArray(colLoc);

    GLint modelLoc =
        glGetUniformLocation(program, "model");

    GLint viewLoc =
        glGetUniformLocation(program, "view");

    GLint projLoc =
        glGetUniformLocation(program, "projection");

    Camera camera(Vec3(0, 0, 3));

    /* ================= LOOP ================= */

    while (running)
    {
        wl_display_dispatch_pending(display);
        wl_display_flush(display);

        glClearColor(
            0.1f,
            0.1f,
            0.1f,
            1.0f);

        glClear(
            GL_COLOR_BUFFER_BIT |
            GL_DEPTH_BUFFER_BIT);

        Mat4 model = Mat4::identity();

        model = Mat4::rotate(
            model,
            (float)clock() / CLOCKS_PER_SEC,
            Vec3(0.5f, 1.0f, 0.0f));

        Mat4 view =
            camera.get_view_matrix();

        Mat4 proj =
            Mat4::perspective(
                45.0f * M_PI / 180.0f,
                current_width / (float)current_height,
                0.1f,
                100.0f);

        glUniformMatrix4fv(
            modelLoc,
            1,
            GL_FALSE,
            model.m);

        glUniformMatrix4fv(
            viewLoc,
            1,
            GL_FALSE,
            view.m);

        glUniformMatrix4fv(
            projLoc,
            1,
            GL_FALSE,
            proj.m);

        glDrawArrays(GL_TRIANGLES, 0, 36);

        eglSwapBuffers(
            egl_display,
            egl_surface);

        usleep(16000);
    }

    /* ================= CLEANUP ================= */

    glDeleteBuffers(1, &vbo);

    eglMakeCurrent(
        egl_display,
        EGL_NO_SURFACE,
        EGL_NO_SURFACE,
        EGL_NO_CONTEXT);

    eglDestroySurface(
        egl_display,
        egl_surface);

    eglDestroyContext(
        egl_display,
        egl_context);

    eglTerminate(egl_display);

    if (keyboard)
        wl_keyboard_destroy(keyboard);

    if (seat)
        wl_seat_destroy(seat);

    wl_egl_window_destroy(egl_window);

    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xdg_surface);

    wl_surface_destroy(surface);

    wl_display_disconnect(display);

    return EXIT_SUCCESS;
}