#include "scroll_manager.h"
#include <algorithm>
#include <map>
#include <memory>

namespace linweb {

// Global scroll states
extern std::map<std::shared_ptr<Node>, float> g_scroll_states;

void apply_scroll_states(std::shared_ptr<LayoutBox> box) {
    if (!box) return;
    if (box->styled_node && box->styled_node->node) {
        if (g_scroll_states.count(box->styled_node->node)) {
            box->scroll_y = g_scroll_states[box->styled_node->node];
        }
    }
    for (auto& child : box->children) {
        apply_scroll_states(child);
    }
}

void save_scroll_state(std::shared_ptr<LayoutBox> box) {
    if (!box || !box->styled_node || !box->styled_node->node) return;
    g_scroll_states[box->styled_node->node] = box->scroll_y;
}

void restore_scroll_state(std::shared_ptr<LayoutBox> box) {
    if (!box || !box->styled_node || !box->styled_node->node) return;
    auto it = g_scroll_states.find(box->styled_node->node);
    if (it != g_scroll_states.end()) {
        box->scroll_y = it->second;
    }
}

float calculate_max_scroll(const std::shared_ptr<LayoutBox>& box, float visible_height, bool is_root_or_body) {
    if (!box) return 0.0f;

    float content_area_height = box->dimensions.content.height;
    // Include padding in the visible area height for max scroll calculation
    float effective_visible_height = visible_height;
    if (!is_root_or_body) {
        effective_visible_height = content_area_height + box->dimensions.padding.top + box->dimensions.padding.bottom;
    }

    return std::max(0.0f, box->total_content_height - effective_visible_height);
}

void clamp_scroll(std::shared_ptr<LayoutBox> box, float max_scroll) {
    if (!box) return;
    if (box->scroll_y < 0) box->scroll_y = 0;
    if (box->scroll_y > max_scroll) box->scroll_y = max_scroll;
}

} // namespace linweb
