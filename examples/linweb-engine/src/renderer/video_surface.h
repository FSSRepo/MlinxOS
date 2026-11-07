#pragma once
#include "gl_wrapper.h"

namespace linweb {

struct VideoFrameTexture {
    GLuint id = 0;       // 0 when there is no such video
    int width = 0;
    int height = 0;
};

// Texture with the current picture of the video `id` (see media/video_player.h). Uploads the newest frame when there is
// one; must be called from the thread that owns the GL context (the renderer).
VideoFrameTexture update_video_texture(int id);

// Releases the textures (call while the GL context is still alive).
void release_video_textures();

} // namespace linweb
