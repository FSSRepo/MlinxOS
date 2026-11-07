#include "renderer/scrollbar.h"
#include "renderer/renderer.h"
#include "renderer/renderer_internal.h"
#include "renderer/matrix_math.h"
#include <algorithm>

namespace linweb {

Scrollbar Scrollbar::from_clip(float clip_x, float clip_y, float clip_w, float clip_h,
                                float content_height, float scroll_y,
                                float opacity)
{
    Scrollbar sb;
    sb.track_x = clip_x + clip_w - 8;
    sb.track_y = clip_y;
    sb.track_w = 8;
    sb.track_h = clip_h;

    float ratio = clip_h / content_height;
    sb.thumb_h = std::max(20.0f, sb.track_h * ratio);
    float max_scroll = content_height - clip_h;
    float scroll_frac = max_scroll > 0.0f ? scroll_y / max_scroll : 0.0f;
    sb.thumb_y = clip_y + scroll_frac * (sb.track_h - sb.thumb_h);

    sb.track_opacity = 0.1f * opacity;
    sb.thumb_opacity = 0.4f * opacity;

    return sb;
}

void Scrollbar::draw() const
{
    auto saved_model = RendererState::current_model;
    RendererState::current_model = mat4_identity();

    Renderer::draw_rect(track_x, track_y, track_w, track_h,
                        0.0f, 0.0f, 0.0f, track_opacity,
                        0.0f, 0.0f, 0.0f, Gradient());
    Renderer::draw_rect(track_x + 1.0f, thumb_y, track_w - 2.0f, thumb_h,
                        0.0f, 0.0f, 0.0f, thumb_opacity,
                        3.0f, 0.0f, 0.0f, Gradient());

    RendererState::current_model = saved_model;
}

} // namespace linweb
