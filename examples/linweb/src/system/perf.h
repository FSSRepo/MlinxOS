#pragma once

namespace linweb {
namespace perf {

struct Stats {
    double style_ms = 0.0;    // style of the frame (or the animation update)
    double layout_ms = 0.0;   // layout tree and layout pass (0 when the frame reused the layout)
    double paint_ms = 0.0;    // Renderer::render
    double script_ms = 0.0;   // requestAnimationFrame callbacks and input handling
    double swap_ms = 0.0;     // buffer swap and event poll: where the wait for the GPU or the display shows up
    double frame_ms = 0.0;    // the whole frame, up to the buffer swap
    double fps = 0.0;         // frames drawn during the last second
};

// Records one drawn frame. The phase times are in milliseconds (the smoothed ones feed stats()); now_s is the
// platform time in seconds, used to count the frames of the last second. layout_calls/layout_hits are the layout
// boxes computed and the ones taken from the cache in the frame; full_rebuild tells if style was matched again.
void record(double script_ms, double style_ms, double layout_ms, double paint_ms, double swap_ms, double frame_ms, double now_s,
            int layout_calls = 0, int layout_hits = 0, bool full_rebuild = false);

// The latest smoothed values.
Stats stats();

// The overlay with the timings: F3 on the keyboard, or R2 on the gamepad through linweb.perf.toggle().
bool visible();
void toggle();

// Draws the profiler over the page, in the top-right corner: the last frames as bars stacked by phase, the average
// of each phase and the layout cache. It is drawn natively, not from the DOM. framebuffer_width is the window width in pixels.
void draw_overlay(int framebuffer_width);

} // namespace perf
} // namespace linweb
