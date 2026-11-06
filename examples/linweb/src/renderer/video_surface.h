#pragma once
#include "gl_wrapper.h"

namespace linweb {

struct VideoFrameTexture {
    GLuint id = 0;       // 0 when no video is loaded
    int width = 0;
    int height = 0;
};

// Texture with the current picture of the video being played. Uploads the newest frame when there is one;
// must be called from the thread that owns the GL context (the renderer).
VideoFrameTexture update_video_texture();

// Releases the texture (call while the GL context is still alive).
void release_video_texture();

} // namespace linweb
