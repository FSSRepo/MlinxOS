#pragma once
#include <memory>
#include "core/dom.h"
#include "layout/layout_engine.h"
#include "parser/css/css_parser.h"
#include "style/style_engine.h"

namespace linweb {

// Why a frame rebuilds the whole page; only used to explain the rebuild in the log.
struct RebuildCause {
    bool viewport_changed = false;
    bool input_changed = false;
    bool layout_animations = false;
};

// Builds the style tree and the layout of the page again (g_style_tree and g_layout_tree) and clears g_dom_dirty.
void rebuild_page(const std::shared_ptr<Node>& dom, const StyleSheet& stylesheet, const std::shared_ptr<Node>& hovered,
                  const Dimensions& viewport, const RebuildCause& cause);

// A frame that does not rebuild: the running transitions and animations are applied to the style tree as it is, and the
// box transforms follow. Only valid while nothing running changes the layout (StyleEngine::has_layout_affecting_animations).
void update_animations_on_tree(const std::shared_ptr<StyledNode>& node, const StyleSheet& stylesheet, double current_time, float w, float h);

} // namespace linweb
