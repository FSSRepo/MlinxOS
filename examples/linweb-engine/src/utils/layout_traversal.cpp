#include "layout_traversal.h"

namespace linweb {

std::shared_ptr<LayoutBox> find_layout_box_at(std::shared_ptr<LayoutBox> box, float x, float y) {
    if (!box) return nullptr;

    // Get total box area (including padding and border)
    // We check against the border box area in screen space
    float bg_x = box->dimensions.content.x - box->dimensions.padding.left - box->dimensions.border.left;
    float bg_y = box->dimensions.content.y - box->dimensions.padding.top - box->dimensions.border.top;
    float bg_w = box->dimensions.content.width + box->dimensions.padding.left + box->dimensions.padding.right + 
                 box->dimensions.border.left + box->dimensions.border.right;
    float bg_h = box->dimensions.content.height + box->dimensions.padding.top + box->dimensions.padding.bottom + 
                 box->dimensions.border.top + box->dimensions.border.bottom;

    // Check if point is inside the border box area
    if (x >= bg_x && x <= bg_x + bg_w && y >= bg_y && y <= bg_y + bg_h) {
        
        // Adjust mouse coordinates for this box's scroll for children
        // IMPORTANT: If this box has scroll, children are shifted by scroll_y.
        // To find which child is at screen position (x, y), we look at (x, y + scroll_y).
        float child_x = x;
        float child_y = y + box->scroll_y;

        // Check children first (top-most elements) in reverse order
        for (auto it = box->children.rbegin(); it != box->children.rend(); ++it) {
            auto found = find_layout_box_at(*it, child_x, child_y);
            if (found) return found;
        }
        
        // Only return if it's an element, otherwise we might be hitting a text node
        if (box->styled_node && box->styled_node->node->type() == NodeType::Element) {
            return box;
        }
    }
    return nullptr;
}

std::shared_ptr<LayoutBox> find_box_for_node(std::shared_ptr<LayoutBox> box, const std::shared_ptr<Node>& node) {
    if (!box || !node) return nullptr;
    if (box->styled_node && box->styled_node->node == node) return box;
    for (const auto& child : box->children) {
        auto found = find_box_for_node(child, node);
        if (found) return found;
    }
    return nullptr;
}

} // namespace linweb
