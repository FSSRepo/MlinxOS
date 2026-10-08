#pragma once
#include "input_handler.h"
#include "../layout/layout_engine.h"

namespace linweb {

class Window;

class ScrollHandler : public InputHandler {
public:
    ScrollHandler();
    ~ScrollHandler() override = default;

    void setup(Window* window) override;
    void update() override;

    void handle_scroll(Window* window, double xoffset, double yoffset);

    void set_window(Window* window) { window_ = window; }
    void set_scroll_speed(float speed) { scroll_speed_ = speed; }
    float get_scroll_speed() const { return scroll_speed_; }

    static void scroll_callback(Window* window, double xoffset, double yoffset);

private:
    Window* window_;
    float scroll_speed_;
};

} // namespace linweb
