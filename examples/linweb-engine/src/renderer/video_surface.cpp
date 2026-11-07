#include "renderer/video_surface.h"
#include "media/video_player.h"
#include <map>

namespace linweb {

namespace {
struct Entry {
    GLuint texture = 0;
    int width = 0;
    int height = 0;
    uint64_t serial = 0;
};
std::map<int, Entry> g_entries;
} // namespace

VideoFrameTexture update_video_texture(int id) {
    int w = 0, h = 0;
    auto found = g_entries.find(id);
    if (!video::session_size(id, w, h)) {
        // the video is gone: its texture too, and the next one with this id starts with a fresh picture
        if (found != g_entries.end()) {
            if (found->second.texture != 0) glDeleteTextures(1, &found->second.texture);
            g_entries.erase(found);
        }
        return {};
    }
    Entry& entry = g_entries[id];

    video::fetch_frame(id, entry.serial, [&entry](const uint8_t* rgb, int width, int height) {
        bool created = entry.texture == 0;
        if (created) glGenTextures(1, &entry.texture);
        glBindTexture(GL_TEXTURE_2D, entry.texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);   // RGB rows are not a multiple of four bytes in general
        if (created || width != entry.width || height != entry.height) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);   // no mipmaps: the size is not a power of two
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            entry.width = width;
            entry.height = height;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb);
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    });

    if (entry.texture == 0) return {};   // nothing decoded yet
    return {entry.texture, entry.width, entry.height};
}

void release_video_textures() {
    for (auto& entry : g_entries) {
        if (entry.second.texture != 0) glDeleteTextures(1, &entry.second.texture);
    }
    g_entries.clear();
}

} // namespace linweb
