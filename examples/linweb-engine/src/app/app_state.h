#pragma once
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "core/dom.h"
#include "style/style_engine.h"
#include "layout/layout_engine.h"
#include "scripting/js_engine.h"

namespace linweb {

// What the page, the input handlers and the debug views share. Each one is written by the main loop (or by a script,
// through the bindings) and read by the others.
extern std::shared_ptr<Node> g_dom_root;
extern std::shared_ptr<StyledNode> g_style_tree;
extern std::shared_ptr<LayoutBox> g_layout_tree;
extern std::shared_ptr<StyledNode> g_hovered_node;
extern std::shared_ptr<Node> g_focused_node;
extern std::shared_ptr<Node> g_active_node;
extern std::map<std::shared_ptr<Node>, float> g_scroll_states;
extern std::vector<std::shared_ptr<Node>> g_detached_nodes;
extern bool g_dom_dirty;               // a script changed the page: the next frame rebuilds style and layout
extern bool g_forms_dirty;             // a form control changed (focus, caret): treated as a change anywhere in the page

// A script changed `node` (its classes, attributes, inline style, text or children): the next frame rebuilds style and
// layout, matching selectors again only around that node. mark_dom_dirty_all() when the change has no single node.
void mark_dom_dirty(const Node* node);
void mark_dom_dirty_all();
// Folds the changes the form controls reported into g_dom_dirty.
void absorb_forms_dirty();
extern bool g_show_node_tree;
extern bool g_debug_mode_active;
extern bool g_show_fps;
extern bool g_show_profiler;
extern std::shared_ptr<Node> g_debug_hovered_node;
extern JSEngine* g_js_engine;
extern float g_scroll_speed;

// Size of the viewport of the current frame; the scroll bindings limit scrollTop with it, as the wheel handler does.
extern float g_viewport_width;
extern float g_viewport_height;

// Rebuilds the pending style and layout now. main.cpp fills it once the stylesheet and the viewport exist; the layout
// bindings call it before measuring, so a measure taken after a script change sees boxes built from the new state.
extern std::function<void()> g_flush_layout;

} // namespace linweb
