#pragma once
#include <memory>
#include <string>
#include "../layout/layout_engine.h"

namespace linweb {

void update_node_tree_hover(const std::shared_ptr<LayoutBox>& box, float& y_offset, int depth, float mx, float my, std::shared_ptr<Node>& hovered_node);
void render_node_tree(const std::shared_ptr<LayoutBox>& box, float& y_offset, int depth, float mx, float my);

void update_dimens_tree_hover(const std::shared_ptr<LayoutBox>& box, float& y_offset, int depth, float mx, float my, std::shared_ptr<Node>& hovered_node);
void render_dimens_tree(const std::shared_ptr<LayoutBox>& box, float& y_offset, int depth, float mx, float my);

} // namespace linweb
