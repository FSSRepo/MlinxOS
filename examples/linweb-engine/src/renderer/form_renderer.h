#pragma once
#include <memory>
#include "layout/layout_engine.h"

namespace linweb {

// Draws the inside of a form control (the text and caret of an <input>, the track and thumb of a slider, the bar of
// a <progress>) on top of the box background and border the renderer already drew. `radius` is the box's border
// radius. Does nothing for other elements.
//
// Colours come from CSS: `color` (text, caret, slider thumb), `accent-color` (filled part and focus ring),
// `track-color` (unfilled part; a non-standard property) and `caret-color`.
void draw_form_control(const std::shared_ptr<LayoutBox>& box, float opacity, float radius);

// The marker of an <li> inside <ul> (a disc) or <ol> (its number), drawn in the indent left of the item. Does nothing
// for other elements.
void draw_list_marker(const std::shared_ptr<LayoutBox>& box, float opacity);

// The list of an open <select>, drawn after the whole tree so that no clipping or later sibling covers it.
void draw_form_overlays(const std::shared_ptr<LayoutBox>& root);

} // namespace linweb
