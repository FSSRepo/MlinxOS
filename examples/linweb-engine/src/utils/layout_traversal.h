#pragma once
#include <memory>
#include "../layout/layout_engine.h"

namespace linweb {

std::shared_ptr<LayoutBox> find_layout_box_at(std::shared_ptr<LayoutBox> box, float x, float y);
std::shared_ptr<LayoutBox> find_box_for_node(std::shared_ptr<LayoutBox> box, const std::shared_ptr<Node>& node);

} // namespace linweb
