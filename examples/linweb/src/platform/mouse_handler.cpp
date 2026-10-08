#include "mouse_handler.h"
#include "window.h"
#include "../utils/layout_traversal.h"
#include "scripting/js_engine.h"

namespace linweb {

// Global state
extern std::shared_ptr<LayoutBox> g_layout_tree;
extern std::shared_ptr<StyledNode> g_hovered_node;
extern std::shared_ptr<Node> g_focused_node;
extern std::shared_ptr<Node> g_active_node;

MouseHandler::MouseHandler() {}

void MouseHandler::setup(Window* window) {
    // Mouse handling is done in update() by polling
}

void MouseHandler::update() {
    // Mouse handling is done externally via handle_hover_and_click
}

void MouseHandler::dispatch_mouseleave_event(JSEngine* js_engine, const std::shared_ptr<Node>& node) {
    if (!node || node->type() != NodeType::Element) return;
    auto el = std::static_pointer_cast<ElementNode>(node);
    
    // Dispatch for memory address
    int64_t addr = reinterpret_cast<int64_t>(node.get());
    js_engine->dispatch_event(std::to_string(addr), "mouseleave");

    // Dispatch for ID
    if (!el->data.id().empty()) {
        js_engine->dispatch_event(el->data.id(), "mouseleave");
    }
    // Dispatch for classes
    for (const auto& cls : el->data.classes()) {
        js_engine->dispatch_event(cls, "mouseleave");
    }
}

void MouseHandler::dispatch_mouseenter_event(JSEngine* js_engine, const std::shared_ptr<Node>& node) {
    if (!node || node->type() != NodeType::Element) return;
    auto el = std::static_pointer_cast<ElementNode>(node);
    
    // Dispatch for memory address
    int64_t addr = reinterpret_cast<int64_t>(node.get());
    js_engine->dispatch_event(std::to_string(addr), "mouseenter");

    // Dispatch for ID
    if (!el->data.id().empty()) {
        js_engine->dispatch_event(el->data.id(), "mouseenter");
    }
    // Dispatch for classes
    for (const auto& cls : el->data.classes()) {
        js_engine->dispatch_event(cls, "mouseenter");
    }
}

void MouseHandler::dispatch_click_event(JSEngine* js_engine, const std::shared_ptr<Node>& node) {
    if (!node || node->type() != NodeType::Element) return;
    auto el = std::static_pointer_cast<ElementNode>(node);

    // Dispatch for memory address (used by addEventListener)
    int64_t addr = reinterpret_cast<int64_t>(node.get());
    js_engine->dispatch_event(std::to_string(addr), "click");

    // Dispatch for ID
    if (!el->data.id().empty()) {
        js_engine->dispatch_event(el->data.id(), "click");
    }
    // Dispatch for classes
    for (const auto& cls : el->data.classes()) {
        js_engine->dispatch_event(cls, "click");
    }

    // Handle inline onclick attribute (e.g. <button onclick="moveCar(-50)">)
    auto it = el->data.attributes.find("onclick");
    if (it != el->data.attributes.end() && !it->second.empty()) {
        js_engine->execute(it->second);
    }
}

void MouseHandler::dispatch_hover_events(JSEngine* js_engine, const std::shared_ptr<Node>& prev_node, const std::shared_ptr<Node>& current_node) {
    if (prev_node) {
        dispatch_mouseleave_event(js_engine, prev_node);
    }
    if (current_node) {
        dispatch_mouseenter_event(js_engine, current_node);
    }
}

void MouseHandler::handle_hover_and_click(Window* window, JSEngine* js_engine) {
    if (!window || !g_layout_tree) return;

    double mx, my;
    window->get_cursor_pos(mx, my);
    
    // Scale mouse coordinates to framebuffer size (for High DPI)
    int ww, wh;
    window->get_window_size(ww, wh);
    int fbw, fbh;
    window->get_framebuffer_size(fbw, fbh);
    state_.px = static_cast<float>(mx) * (static_cast<float>(fbw) / ww);
    state_.py = static_cast<float>(my) * (static_cast<float>(fbh) / wh);

    auto hovered_box = find_layout_box_at(g_layout_tree, state_.px, state_.py);
    
    state_.current_hovered_dom_node = hovered_box ? hovered_box->styled_node->node : nullptr;
    
    // CSS :hover support - Dispatch events
    auto prev_hovered = g_hovered_node ? g_hovered_node->node : nullptr;
    if (state_.current_hovered_dom_node != prev_hovered) {
        dispatch_hover_events(js_engine, prev_hovered, state_.current_hovered_dom_node);
        
        g_hovered_node = state_.current_hovered_dom_node ? std::make_shared<StyledNode>(state_.current_hovered_dom_node) : nullptr;
    }

    // Handle Click
    state_.had_click_event = false;
    bool pressed = window->is_mouse_button_pressed(MouseButton::Left);
    if (pressed && !state_.pressed) {
        state_.pressed = true;
        state_.had_click_event = true;
        g_active_node = state_.current_hovered_dom_node;
        if (state_.current_hovered_dom_node && state_.current_hovered_dom_node->type() == NodeType::Element) {
            g_focused_node = state_.current_hovered_dom_node;
            dispatch_click_event(js_engine, state_.current_hovered_dom_node);
        }
    } else if (!pressed) {
        state_.pressed = false;
        g_active_node = nullptr;
    }
}

} // namespace linweb
