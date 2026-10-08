#pragma once
#include <memory>
#include "../layout/layout_engine.h"

namespace linweb {

void apply_scroll_states(std::shared_ptr<LayoutBox> box);
void save_scroll_state(std::shared_ptr<LayoutBox> box);
void restore_scroll_state(std::shared_ptr<LayoutBox> box);
float calculate_max_scroll(const std::shared_ptr<LayoutBox>& box, float visible_height, bool is_root_or_body);
void clamp_scroll(std::shared_ptr<LayoutBox> box, float max_scroll);

} // namespace linweb
