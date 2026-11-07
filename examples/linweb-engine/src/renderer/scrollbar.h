#pragma once

namespace linweb {

struct Scrollbar {
    float track_x, track_y, track_w, track_h;
    float thumb_y, thumb_h;
    float track_opacity = 1.0f;
    float thumb_opacity = 1.0f;

    static Scrollbar from_clip(float clip_x, float clip_y, float clip_w, float clip_h,
                               float content_height, float scroll_y,
                               float opacity = 1.0f);

    void draw() const;
};

} // namespace linweb
