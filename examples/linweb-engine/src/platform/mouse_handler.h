#pragma once
#include <memory>
#include <vector>
#include <string>
#include "input_handler.h"
#include "keys.h"
#include "../core/dom.h"
#include "../layout/layout_engine.h"
#include "../style/style_engine.h"

namespace linweb {

class Window;
class JSEngine;

struct MouseState {
    double x = 0.0;
    double y = 0.0;
    float px = 0.0f;
    float py = 0.0f;
    bool pressed = false;
    bool had_click_event = false;
    std::shared_ptr<Node> prev_hovered_dom_node;
    std::shared_ptr<Node> current_hovered_dom_node;
};

class MouseHandler : public InputHandler {
public:
    MouseHandler();
    ~MouseHandler() override = default;

    void setup(Window* window) override;
    void update() override;

    void handle_hover_and_click(Window* window, JSEngine* js_engine);
    void dispatch_hover_events(JSEngine* js_engine, const std::shared_ptr<Node>& prev_node, const std::shared_ptr<Node>& current_node);
    void dispatch_click_event(JSEngine* js_engine, const std::shared_ptr<Node>& node);
    void dispatch_mouseleave_event(JSEngine* js_engine, const std::shared_ptr<Node>& node);
    void dispatch_mouseenter_event(JSEngine* js_engine, const std::shared_ptr<Node>& node);

    const MouseState& get_state() const { return state_; }
    MouseState& get_state() { return state_; }

private:
    MouseState state_;
};

} // namespace linweb
