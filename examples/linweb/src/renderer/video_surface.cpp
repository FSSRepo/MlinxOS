#include "renderer/video_surface.h"
#include "media/video/video_player.h"

namespace linweb {

namespace {
GLuint g_texture = 0;
int g_width = 0;
int g_height = 0;
uint64_t g_serial = 0;
} // namespace

VideoFrameTexture update_video_texture() {
    int w = 0, h = 0;
    if (!video::current_size(w, h)) {
        g_serial = 0;   // the next video starts with a fresh picture
        return {};
    }

    video::fetch_frame(g_serial, [](const uint8_t* rgb, int width, int height) {
        bool created = g_texture == 0;
        if (created) glGenTextures(1, &g_texture);
        glBindTexture(GL_TEXTURE_2D, g_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);   // RGB rows are not a multiple of four bytes in general
        if (created || width != g_width || height != g_height) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);   // no mipmaps: the size is not a power of two
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            g_width = width;
            g_height = height;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb);
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    });

    if (g_texture == 0) return {};   // nothing decoded yet
    return {g_texture, g_width, g_height};
}

void release_video_texture() {
    if (g_texture != 0) glDeleteTextures(1, &g_texture);
    g_texture = 0;
    g_width = g_height = 0;
    g_serial = 0;
}

} // namespace linweb
