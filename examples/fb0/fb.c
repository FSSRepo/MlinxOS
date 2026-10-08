#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <string.h>
#include <stdint.h>

int main(void)
{
    int fb = open("/dev/fb0", O_RDWR);
    if (fb < 0) {
        perror("open /dev/fb0");
        return 1;
    }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    if (ioctl(fb, FBIOGET_VSCREENINFO, &vinfo)) {
        perror("FBIOGET_VSCREENINFO");
        return 1;
    }

    if (ioctl(fb, FBIOGET_FSCREENINFO, &finfo)) {
        perror("FBIOGET_FSCREENINFO");
        return 1;
    }

    int width  = vinfo.xres;
    int height = vinfo.yres;
    int bpp    = vinfo.bits_per_pixel;

    printf("fb0: %dx%d %dbpp\n", width, height, bpp);

    size_t screensize = finfo.line_length * height;
    uint8_t *buffer = malloc(screensize);
    if (!buffer) {
        perror("malloc");
        return 1;
    }

    for (int frame = 0; frame < 600; frame++) {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {

                uint8_t r = (x * 255) / width;
                uint8_t g = (y * 255) / height;
                uint8_t b = (frame % 255);

                long location = x * (bpp / 8) + y * finfo.line_length;

                if (bpp == 32) {
                    buffer[location + 0] = b;
                    buffer[location + 1] = g;
                    buffer[location + 2] = r;
                    buffer[location + 3] = 0x00;
                }
                else if (bpp == 16) {
                    uint16_t pixel = ((r >> 3) << 11) |
                                     ((g >> 2) << 5)  |
                                     (b >> 3);
                    *((uint16_t*)(buffer + location)) = pixel;
                }
            }
        }

        // Dump the whole buffer to the framebuffer
        if (lseek(fb, 0, SEEK_SET) < 0) {
            perror("lseek");
            break;
        }

        if (write(fb, buffer, screensize) != screensize) {
            perror("write");
            break;
        }

        usleep(16000); // ~60 FPS
    }

    free(buffer);
    close(fb);

    return 0;
}
