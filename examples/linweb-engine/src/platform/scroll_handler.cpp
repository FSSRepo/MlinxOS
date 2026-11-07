#include "scroll_handler.h"
#include "window.h"
#include <algorithm>
#include <cmath>
#include "../utils/layout_traversal.h"
#include "../utils/scroll_manager.h"

namespace linweb {

// Global state
extern std::shared_ptr<LayoutBox> g_layout_tree;
extern float g_scroll_speed;
extern std::map<std::shared_ptr<Node>, float> g_scroll_states;

static ScrollHandler* g_scroll_handler_instance = nullptr;

ScrollHandler::ScrollHandler() : window_(nullptr), scroll_speed_(40.0f) {
    g_scroll_handler_instance = this;
}

void ScrollHandler::setup(Window* window) {
    window_ = window;
    if (window) {
        window->set_scroll_callback(scroll_callback);
    }
}

void ScrollHandler::update() {
    // Scroll is event-driven; nothing to do in update
}

void ScrollHandler::handle_scroll(Window* window, double xoffset, double yoffset) {
    if (!g_layout_tree || !window) return;

    double mx, my;
    window->get_cursor_pos(mx, my);

    int ww, wh;
    window->get_window_size(ww, wh);
    int fbw, fbh;
    window->get_framebuffer_size(fbw, fbh);
    float px = static_cast<float>(mx) * (static_cast<float>(fbw) / ww);
    float py = static_cast<float>(my) * (static_cast<float>(fbh) / wh);

    auto hovered_box = find_layout_box_at(g_layout_tree, px, py);
    
    // Find first scrollable parent (or the box itself)
    auto current = hovered_box;
    while (current) {
        std::string overflow = current->styled_node->value("overflow");
        
        // Root element (HTML/Body) should always be scrollable if it has overflow: auto/scroll
        // or if it's the top-level layout tree and we want global scroll.
        bool is_root = (current == g_layout_tree);

        bool can_scroll = (overflow == "scroll" || overflow == "auto" || is_root) && overflow != "hidden";

        if (can_scroll) {
            float visible_height = current->dimensions.content.height + current->dimensions.padding.top + current->dimensions.padding.bottom;

            if (is_root) {
                int fbw, fbh;
                window->get_framebuffer_size(fbw, fbh);
                visible_height = (float)fbh;
            }

            float scrollable_height = current->total_content_height - current->dimensions.border.top - current->dimensions.border.bottom;
            float max_scroll = std::max(0.0f, scrollable_height - visible_height);

            if (max_scroll <= 0.0f) {
                current = current->parent.lock();
                continue;
            }

            current->scroll_y -= (float)yoffset * scroll_speed_;

            if (current->scroll_y < 0) current->scroll_y = 0;
            if (current->scroll_y > max_scroll) current->scroll_y = max_scroll;

            if (current->styled_node && current->styled_node->node) {
                g_scroll_states[current->styled_node->node] = current->scroll_y;
            }

            break;
        }
        current = current->parent.lock();
    }
}

void ScrollHandler::scroll_callback(Window* window, double xoffset, double yoffset) {
    if (g_scroll_handler_instance) {
        g_scroll_handler_instance->handle_scroll(window, xoffset, yoffset);
    }
}

} // namespace linweb
