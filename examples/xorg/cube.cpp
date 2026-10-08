#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <ctime>
#include <X11/keysym.h>

#include "ogl_math.h"
#include "camera.h"

/* =================== SHADERS =================== */

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

/* =================== SHADER UTILS =================== */

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
        printf("Shader error: %s\n", log);
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
        printf("Link error: %s\n", log);
        exit(1);
    }
    return program;
}

/* =================== MAIN =================== */

int main()
{
    /* ---------- X11 ---------- */
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy)
    {
        printf("Cannot open X display\n");
        return -1;
    }

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    int width = DisplayWidth(dpy, screen);
    int height = DisplayHeight(dpy, screen);

    XSetWindowAttributes swa;
    swa.override_redirect = False;
    swa.event_mask = ExposureMask | KeyPressMask;

    Window win = XCreateWindow(
        dpy, root,
        0, 0, width, height,
        0,
        CopyFromParent,
        InputOutput,
        CopyFromParent,
        CWEventMask,
        &swa);

    XMapWindow(dpy, win);
    XFlush(dpy);

    Atom wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom wm_fullscr = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);

    XEvent xev{};
    xev.type = ClientMessage;
    xev.xclient.window = win;
    xev.xclient.message_type = wm_state;
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = 1; // _NET_WM_STATE_ADD
    xev.xclient.data.l[1] = wm_fullscr;
    xev.xclient.data.l[2] = 0;

    XSendEvent(
        dpy,
        DefaultRootWindow(dpy),
        False,
        SubstructureRedirectMask | SubstructureNotifyMask,
        &xev);

    /* ───────── EGL ───────── */
    EGLDisplay egl_dpy = eglGetDisplay((EGLNativeDisplayType)dpy);
    eglInitialize(egl_dpy, NULL, NULL);

    EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE};

    EGLConfig cfg;
    EGLint num;
    eglChooseConfig(egl_dpy, attribs, &cfg, 1, &num);

    EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE};

    EGLContext ctx = eglCreateContext(
        egl_dpy,
        cfg,
        EGL_NO_CONTEXT,
        ctx_attribs);
    EGLSurface surf = eglCreateWindowSurface(egl_dpy, cfg, (EGLNativeWindowType)win, NULL);
    eglMakeCurrent(egl_dpy, surf, surf, ctx);

    /* ---------- GL ---------- */
    glViewport(0, 0, width, height);
    glEnable(GL_DEPTH_TEST);

    GLuint program = createProgram();
    glUseProgram(program);

    /* ---------- CUBE DATA ---------- */
    float vertices[] = {
        // Position           // Color
        -0.5f,
        -0.5f,
        0.5f,
        1.0f,
        0.0f,
        0.0f, // Front
        0.5f,
        -0.5f,
        0.5f,
        1.0f,
        0.0f,
        0.0f,
        0.5f,
        0.5f,
        0.5f,
        1.0f,
        0.0f,
        0.0f,
        0.5f,
        0.5f,
        0.5f,
        1.0f,
        0.0f,
        0.0f,
        -0.5f,
        0.5f,
        0.5f,
        1.0f,
        0.0f,
        0.0f,
        -0.5f,
        -0.5f,
        0.5f,
        1.0f,
        0.0f,
        0.0f,

        -0.5f,
        -0.5f,
        -0.5f,
        0.0f,
        1.0f,
        0.0f, // Back
        0.5f,
        -0.5f,
        -0.5f,
        0.0f,
        1.0f,
        0.0f,
        0.5f,
        0.5f,
        -0.5f,
        0.0f,
        1.0f,
        0.0f,
        0.5f,
        0.5f,
        -0.5f,
        0.0f,
        1.0f,
        0.0f,
        -0.5f,
        0.5f,
        -0.5f,
        0.0f,
        1.0f,
        0.0f,
        -0.5f,
        -0.5f,
        -0.5f,
        0.0f,
        1.0f,
        0.0f,

        -0.5f,
        0.5f,
        0.5f,
        0.0f,
        0.0f,
        1.0f, // Top
        -0.5f,
        0.5f,
        -0.5f,
        0.0f,
        0.0f,
        1.0f,
        0.5f,
        0.5f,
        -0.5f,
        0.0f,
        0.0f,
        1.0f,
        0.5f,
        0.5f,
        -0.5f,
        0.0f,
        0.0f,
        1.0f,
        0.5f,
        0.5f,
        0.5f,
        0.0f,
        0.0f,
        1.0f,
        -0.5f,
        0.5f,
        0.5f,
        0.0f,
        0.0f,
        1.0f,

        -0.5f,
        -0.5f,
        0.5f,
        1.0f,
        1.0f,
        0.0f, // Bottom
        -0.5f,
        -0.5f,
        -0.5f,
        1.0f,
        1.0f,
        0.0f,
        0.5f,
        -0.5f,
        -0.5f,
        1.0f,
        1.0f,
        0.0f,
        0.5f,
        -0.5f,
        -0.5f,
        1.0f,
        1.0f,
        0.0f,
        0.5f,
        -0.5f,
        0.5f,
        1.0f,
        1.0f,
        0.0f,
        -0.5f,
        -0.5f,
        0.5f,
        1.0f,
        1.0f,
        0.0f,

        0.5f,
        -0.5f,
        0.5f,
        1.0f,
        0.0f,
        1.0f, // Right
        0.5f,
        -0.5f,
        -0.5f,
        1.0f,
        0.0f,
        1.0f,
        0.5f,
        0.5f,
        -0.5f,
        1.0f,
        0.0f,
        1.0f,
        0.5f,
        0.5f,
        -0.5f,
        1.0f,
        0.0f,
        1.0f,
        0.5f,
        0.5f,
        0.5f,
        1.0f,
        0.0f,
        1.0f,
        0.5f,
        -0.5f,
        0.5f,
        1.0f,
        0.0f,
        1.0f,

        -0.5f,
        -0.5f,
        0.5f,
        0.0f,
        1.0f,
        1.0f, // Left
        -0.5f,
        -0.5f,
        -0.5f,
        0.0f,
        1.0f,
        1.0f,
        -0.5f,
        0.5f,
        -0.5f,
        0.0f,
        1.0f,
        1.0f,
        -0.5f,
        0.5f,
        -0.5f,
        0.0f,
        1.0f,
        1.0f,
        -0.5f,
        0.5f,
        0.5f,
        0.0f,
        1.0f,
        1.0f,
        -0.5f,
        -0.5f,
        0.5f,
        0.0f,
        1.0f,
        1.0f,
    };

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    GLint posLoc = glGetAttribLocation(program, "aPos");
    GLint colLoc = glGetAttribLocation(program, "aColor");

    glVertexAttribPointer(posLoc, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(posLoc);

    glVertexAttribPointer(colLoc, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(colLoc);

    GLint modelLoc = glGetUniformLocation(program, "model");
    GLint viewLoc = glGetUniformLocation(program, "view");
    GLint projLoc = glGetUniformLocation(program, "projection");

    Camera camera(Vec3(0, 0, 3));

    /* ---------- LOOP ---------- */
    bool running = true;
    while (running)
    {
        while (XPending(dpy))
        {
            XEvent ev;
            XNextEvent(dpy, &ev);

            if (ev.type == KeyPress)
            {
                KeySym key = XLookupKeysym(&ev.xkey, 0);

                if (key == XK_Escape)
                {
                    running = false;
                }
            }
        }

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        Mat4 model = Mat4::identity();
        model = Mat4::rotate(model, (float)clock() / CLOCKS_PER_SEC, Vec3(0.5f, 1, 0));

        Mat4 view = camera.get_view_matrix();
        Mat4 proj = Mat4::perspective(45.0f * M_PI / 180.0f, (width/ (float)height), 0.1f, 100.f);

        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, model.m);
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, view.m);
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, proj.m);

        glDrawArrays(GL_TRIANGLES, 0, 36);

        eglSwapBuffers(egl_dpy, surf);
        usleep(16000);
    }

    eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl_dpy, surf);
    eglDestroyContext(egl_dpy, ctx);
    eglTerminate(egl_dpy);

    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
}
