#include <GL/osmesa.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define die(...)                      \
    do                                \
    {                                 \
        fprintf(stderr, __VA_ARGS__); \
        exit(1);                      \
    } while (0)

int main(void)
{
    // ────────────────────────────────────────────────
    //  Open the framebuffer and get its information
    // ────────────────────────────────────────────────
    int fb = open("/dev/fb0", O_RDWR);
    if (fb < 0)
        die("Could not open /dev/fb0: %m\n");

    struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo vinfo;

    if (ioctl(fb, FBIOGET_FSCREENINFO, &finfo) < 0)
        die("FBIOGET_FSCREENINFO failed: %m\n");

    if (ioctl(fb, FBIOGET_VSCREENINFO, &vinfo) < 0)
        die("FBIOGET_VSCREENINFO failed: %m\n");

    int width = vinfo.xres;
    int height = vinfo.yres;
    int bpp = vinfo.bits_per_pixel;
    int stride = finfo.line_length; // <- very important

    printf("Framebuffer: %dx%d  %dbpp   stride=%d  type=%d\n",
           width, height, bpp, stride, finfo.type);

    if (bpp != 32 && bpp != 24)
    {
        fprintf(stderr, "Only 24/32 bpp supported in this example\n");
        close(fb);
        return 1;
    }

    // ────────────────────────────────────────────────
    //  Create OSMesa context (RGBA by default)
    // ────────────────────────────────────────────────
    uint32_t *os_buffer = calloc(width * height, 4);
    if (!os_buffer)
        die("No memory for OSMesa buffer\n");

    OSMesaContext ctx = OSMesaCreateContextExt(OSMESA_RGBA, 16, 0, 0, NULL);
    if (!ctx)
        die("Could not create OSMesa context\n");

    if (!OSMesaMakeCurrent(ctx, os_buffer, GL_UNSIGNED_BYTE, width, height))
        die("OSMesaMakeCurrent failed\n");

    glViewport(0, 0, width, height);

    // ────────────────────────────────────────────────
    //  Simple shader (same as yours)
    // ────────────────────────────────────────────────
    const char *vs_src =
        "#version 120\n"
        "attribute vec2 position;\n"
        "attribute vec3 color;\n"
        "varying   vec3 vColor;\n"
        "uniform   float offsetY;\n"
        "void main() {\n"
        "    vColor = color;\n"
        "    gl_Position = vec4(position.x, position.y + offsetY, 0.0, 1.0);\n"
        "}\n";

    const char *fs_src =
        "#version 120\n"
        "varying vec3 vColor;\n"
        "void main() {\n"
        "    gl_FragColor = vec4(vColor, 1.0);\n"
        "}\n";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vs_src, NULL);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fs_src, NULL);
    glCompileShader(fs);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glUseProgram(prog);

    GLint pos_loc = glGetAttribLocation(prog, "position");
    GLint color_loc = glGetAttribLocation(prog, "color");
    GLint offset_loc = glGetUniformLocation(prog, "offsetY");

    // Centered triangle (NDC coordinates)
    float vertices[] = {
        -0.20f,
        -0.20f,
        1.0f,
        0.0f,
        0.0f,
        0.20f,
        -0.20f,
        0.0f,
        1.0f,
        0.0f,
        0.00f,
        0.35f,
        0.0f,
        0.0f,
        1.0f,
    };

    glEnableVertexAttribArray(pos_loc);
    glVertexAttribPointer(pos_loc, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), vertices);

    glEnableVertexAttribArray(color_loc);
    glVertexAttribPointer(color_loc, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), vertices + 2);

    // Temporary buffer to write a line (aligned to the stride)
    uint8_t *line_buf = malloc(stride);
    if (!line_buf)
        die("No memory for line\n");

    // ────────────────────────────────────────────────
    //  Animation loop
    // ────────────────────────────────────────────────
    float t = 0.0f;
    uint8_t *fb_line_buffer = aligned_alloc(32, stride); // or malloc + manual alignment
    while (1)
    {
        float offsetY = 0.4f * sinf(t);
        t += 0.033f; // ~30 degrees per frame @60fps

        glClearColor(0.1f, 0.12f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUniform1f(offset_loc, offsetY);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();

        // Prepare the whole framebuffer in memory (converting RGBA -> fb format)
        uint8_t *src = (uint8_t *)os_buffer;
        uint8_t *dst = fb_line_buffer; // but we would actually use a large buffer

        // Option A: large intermediate buffer (more memory but faster)
        static uint8_t *full_fb_copy = NULL;
        if (!full_fb_copy)
            full_fb_copy = malloc((size_t)height * stride);

        for (int y = 0; y < height; y++)
        {
            int src_y = height - 1 - y;
            uint8_t *row_src = src + src_y * width * 4;
            uint8_t *row_dst = full_fb_copy + y * stride;

            for (int x = 0; x < width; x++)
            {
                uint8_t r = row_src[x * 4 + 0];
                uint8_t g = row_src[x * 4 + 1];
                uint8_t b = row_src[x * 4 + 2];

                // Adjust according to your framebuffer (try both)
                row_dst[x * 4 + 0] = b; // common BGRA
                row_dst[x * 4 + 1] = g;
                row_dst[x * 4 + 2] = r;
                row_dst[x * 4 + 3] = 0xFF;
            }
        }

        // A single write!
        if (lseek(fb, 0, SEEK_SET) < 0)
            perror("lseek");
        if (write(fb, full_fb_copy, (size_t)height * stride) != (ssize_t)height * stride)
        {
            perror("write full frame");
        }

        nanosleep(&(struct timespec){.tv_nsec = 16666666}, NULL); // ~60 fps
    }

    // Cleanup (never reached in this infinite loop)
    free(line_buf);
    free(os_buffer);
    OSMesaDestroyContext(ctx);
    close(fb);
    return 0;
}