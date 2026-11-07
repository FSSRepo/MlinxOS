#include "app/app_state.h"

namespace linweb {

std::shared_ptr<Node> g_dom_root;
std::shared_ptr<StyledNode> g_style_tree;
std::shared_ptr<LayoutBox> g_layout_tree;
std::shared_ptr<StyledNode> g_hovered_node;
std::shared_ptr<Node> g_focused_node;
std::shared_ptr<Node> g_active_node;
std::map<std::shared_ptr<Node>, float> g_scroll_states;
std::vector<std::shared_ptr<Node>> g_detached_nodes;
bool g_dom_dirty = false;
bool g_forms_dirty = false;

void mark_dom_dirty(const Node* node) {
    g_dom_dirty = true;
    StyleEngine::invalidate_matched_subtree(node);
}

void mark_dom_dirty_all() {
    g_dom_dirty = true;
    StyleEngine::invalidate_matched_all();
}

void absorb_forms_dirty() {
    if (!g_forms_dirty) return;
    g_forms_dirty = false;
    mark_dom_dirty_all();
}
bool g_show_node_tree = false;
bool g_debug_mode_active = false;
bool g_show_fps = false;
bool g_show_profiler = false;
std::shared_ptr<Node> g_debug_hovered_node = nullptr;
JSEngine* g_js_engine = nullptr;
float g_scroll_speed = 40.0f;
float g_viewport_width = 0.0f;
float g_viewport_height = 0.0f;
std::function<void()> g_flush_layout;

} // namespace linweb
