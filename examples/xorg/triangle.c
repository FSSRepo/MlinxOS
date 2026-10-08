#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>

/* Simple shaders */
const char *vertex_shader_src =
"attribute vec2 aPos;\n"
"uniform float uAngle;\n"
"void main() {\n"
"    float c = cos(uAngle);\n"
"    float s = sin(uAngle);\n"
"    gl_Position = vec4(c*aPos.x - s*aPos.y, s*aPos.x + c*aPos.y, 0.0, 1.0);\n"
"}\n";

const char *fragment_shader_src =
"precision mediump float;\n"
"void main() {\n"
"    gl_FragColor = vec4(1.0, 0.5, 0.2, 1.0);\n"
"}\n";

/* Shader compilation function */
GLuint compile_shader(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char buf[512];
        glGetShaderInfoLog(shader, 512, NULL, buf);
        fprintf(stderr, "Shader compile error: %s\n", buf);
    }
    return shader;
}

int main(void)
{
    /* ───────── X11 ───────── */
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "No X display\n"); return 1; }

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    int width  = DisplayWidth(dpy, screen);
    int height = DisplayHeight(dpy, screen);

    XSetWindowAttributes swa;
    swa.override_redirect = True;
    swa.event_mask = ExposureMask;

    Window win = XCreateWindow(
        dpy, root,
        0, 0, width, height,
        0, CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWEventMask, &swa
    );
    XMapRaised(dpy, win);
    XFlush(dpy);

    /* ───────── EGL ───────── */
    EGLDisplay egl_dpy = eglGetDisplay((EGLNativeDisplayType)dpy);
    eglInitialize(egl_dpy, NULL, NULL);

    EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg;
    EGLint num;
    eglChooseConfig(egl_dpy, attribs, &cfg, 1, &num);

    EGLContext ctx = eglCreateContext(egl_dpy, cfg, EGL_NO_CONTEXT, (EGLint[]){EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE});
    EGLSurface surf = eglCreateWindowSurface(egl_dpy, cfg, (EGLNativeWindowType)win, NULL);
    eglMakeCurrent(egl_dpy, surf, surf, ctx);

    /* ───────── OpenGL ES ───────── */
    glViewport(0, 0, width, height);

    /* Compile shaders and create program */
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_src);
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glUseProgram(program);

    /* Triangle vertices (centered) */
    GLfloat vertices[] = {
        0.0f,  0.5f,
       -0.5f, -0.5f,
        0.5f, -0.5f
    };

    GLuint pos_loc = glGetAttribLocation(program, "aPos");
    GLuint angle_loc = glGetUniformLocation(program, "uAngle");

    glEnableVertexAttribArray(pos_loc);
    glVertexAttribPointer(pos_loc, 2, GL_FLOAT, GL_FALSE, 0, vertices);

    /* Render loop */
    for (int i = 0; i < 600; i++) {
        float angle = (float)i / 600.0f * 2.0f * M_PI; // full rotation

        glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUniform1f(angle_loc, angle);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        eglSwapBuffers(egl_dpy, surf);
        usleep(16000); // ~60 FPS
    }

    /* ───────── Cleanup ───────── */
    eglDestroySurface(egl_dpy, surf);
    eglDestroyContext(egl_dpy, ctx);
    eglTerminate(egl_dpy);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);

    return 0;
}
