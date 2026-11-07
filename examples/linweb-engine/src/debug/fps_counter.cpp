#include "debug/fps_counter.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include "app/app_state.h"
#include "platform/platform_config.h"
#include "renderer/renderer.h"

namespace linweb {

FpsCounter::FpsCounter() : log_(std::getenv("LINWEB_FPS") != nullptr), window_start_(get_platform_time())
{
    if (log_) g_show_fps = true;
}

void FpsCounter::draw() const
{
    if (!g_show_fps) return;
    char text[32];
    snprintf(text, sizeof(text), "%.0f FPS", value_);
    Renderer::draw_rect(6.0f, 6.0f, 86.0f, 22.0f, 0.0f, 0.0f, 0.0f, 0.7f, 4.0f);
    Renderer::draw_text(text, 12.0f, 22.0f, 14.0f, 0.4f, 1.0f, 0.4f, 1.0f, true);
}

void FpsCounter::frame_done()
{
    if (!g_show_fps) return;
    ++frames_;
    const double now = get_platform_time();
    if (now - window_start_ >= 0.5) {
        value_ = frames_ / (now - window_start_);
        if (log_) std::cout << "FPS: " << value_ << std::endl;
        window_start_ = now;
        frames_ = 0;
    }
}

} // namespace linweb
