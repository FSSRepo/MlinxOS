#include "debug/layout_log.h"
#include "utils/log.h"

namespace linweb {

namespace {

void log_box_tree(const LayoutBox& box, int depth, int& count)
{
    ++count;
    if (box.styled_node && box.styled_node->node) {
        const std::string id = box.styled_node->node->data.id();
        if (id == "submenu-container" || id == "main-menu" || id == "xmb-container" || id == "status" || id == "browser-preview-label") {
            log_event("layout", "depth " + std::to_string(depth) + " #" + id + " x=" + std::to_string(box.dimensions.content.x) +
                                    " y=" + std::to_string(box.dimensions.content.y) +
                                    " w=" + std::to_string(box.dimensions.content.width) +
                                    " h=" + std::to_string(box.dimensions.content.height) +
                                    " children=" + std::to_string(box.children.size()) +
                                    " total=" + std::to_string(box.total_content_height));
        }
    }
    for (const auto& child : box.children) log_box_tree(*child, depth + 1, count);
}

} // namespace

void log_layout_snapshot(const LayoutBox& root)
{
    int count = 0;
    log_box_tree(root, 0, count);
    log_event("layout", "rebuilt: " + std::to_string(count) + " boxes");
}

} // namespace linweb
