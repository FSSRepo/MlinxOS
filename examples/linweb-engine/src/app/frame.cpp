#include "app/frame.h"
#include "app/app_state.h"
#include "debug/layout_log.h"
#include "debug/profiler.h"
#include "platform/platform_config.h"
#include "style/animation_engine.h"
#include "style/transform_parser.h"
#include "style/transition_engine.h"
#include <string>
#include "utils/log.h"
#include "utils/scroll_manager.h"

namespace linweb {

void update_animations_on_tree(const std::shared_ptr<StyledNode>& node, const StyleSheet& stylesheet, double current_time, float w, float h) {
    if (!node) return;
    TransitionEngine::apply_transitions(node, current_time, w, h);
    AnimationEngine::apply_animations(node, stylesheet, current_time);
    // the transform comes from the (possibly animated) value, as in build_style_tree
    const std::string transform_str = node->value("transform");
    if (!transform_str.empty()) {
        node->transform = TransformParser::parse_transform(transform_str);
        const std::string origin_str = node->value("transform-origin");
        if (!origin_str.empty()) TransformParser::parse_transform_origin(origin_str, node->transform);
    }
    // `translate` is re-read each frame; the transform it adds to starts again from the transform property
    if (!node->value("translate").empty() && transform_str.empty()) node->transform = Transform();
    TransformParser::apply_translate_property(node->value("translate"), node->transform);
    for (const auto& child : node->children) {
        update_animations_on_tree(child, stylesheet, current_time, w, h);
    }
}

void rebuild_page(const std::shared_ptr<Node>& dom, const StyleSheet& stylesheet, const std::shared_ptr<Node>& hovered,
                  const Dimensions& viewport, const RebuildCause& cause)
{
    const double tp0 = get_platform_time();
    g_layout_calls = 0;
    g_layout_hits = 0;
    const bool was_dirty = g_dom_dirty;
    g_dom_dirty = false;
    // A frame that only moves a running transition or animation matches no selector again: the page, the hover, the
    // focus and the stylesheet are the ones of the last full build, so what the selectors gave still holds.
    static const Node* last_hovered = nullptr;
    static const Node* last_focused = nullptr;
    static const Node* last_active = nullptr;
    const bool same_inputs = hovered.get() == last_hovered && g_focused_node.get() == last_focused && g_active_node.get() == last_active;
    // A frame after a script change matches again only the nodes around what the script touched.
    const bool inputs_held = !cause.viewport_changed && !cause.input_changed && same_inputs && g_style_tree;
    const bool animation_only = cause.layout_animations && !was_dirty && inputs_held;
    StyleEngine::set_reuse_matched_styles(inputs_held && (cause.layout_animations || was_dirty) && (!was_dirty || StyleEngine::matched_scoped()));
    last_hovered = hovered.get();
    last_focused = g_focused_node.get();
    last_active = g_active_node.get();
    g_style_tree = StyleEngine::build_style_tree(dom, stylesheet, hovered, g_focused_node, g_active_node, get_platform_time(), viewport.content.width, viewport.content.height);
    const double tp1 = get_platform_time();
    g_layout_tree = LayoutEngine::build_layout_tree(g_style_tree);
    const double tp2 = get_platform_time();
    double tp3 = tp2;
    if (g_layout_tree) {
        apply_scroll_states(g_layout_tree);
        g_layout_tree->layout(viewport);
        tp3 = get_platform_time();
        if (log_enabled()) log_layout_snapshot(*g_layout_tree);
    }
    Profiler::add(ProfSection::Style, (tp1 - tp0) * 1000.0);
    Profiler::add(ProfSection::Tree, (tp2 - tp1) * 1000.0);
    Profiler::add(ProfSection::Layout, (tp3 - tp2) * 1000.0);
    Profiler::note_rebuild(g_layout_calls, g_layout_hits, !animation_only);
    if (log_enabled()) log_event("perf", "rebuild style=" + std::to_string((tp1 - tp0) * 1000.0) + "ms tree=" + std::to_string((tp2 - tp1) * 1000.0) + "ms layout=" + std::to_string((tp3 - tp2) * 1000.0) + "ms layout_calls=" + std::to_string(g_layout_calls) + " hits=" + std::to_string(g_layout_hits) + " vp=" + std::to_string(cause.viewport_changed) + " input=" + std::to_string(cause.input_changed) + " anim=" + std::to_string(cause.layout_animations) + " dirty=" + std::to_string(was_dirty));
}

} // namespace linweb
