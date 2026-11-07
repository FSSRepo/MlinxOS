#pragma once

namespace linweb {

// The frames per second the engine reaches, measured over half a second, drawn in a corner over the page (F3 or --fps).
// With LINWEB_FPS set it also prints the figure to the console.
class FpsCounter {
public:
    FpsCounter();
    void draw() const;     // the overlay, when it is turned on
    void frame_done();     // call once per frame, after swapping buffers

private:
    bool log_;
    double window_start_;
    int frames_ = 0;
    double value_ = 0.0;
};

} // namespace linweb
