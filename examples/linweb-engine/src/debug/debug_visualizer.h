#pragma once
#include <memory>
#include "../core/dom.h"
#include "../layout/layout_engine.h"

namespace linweb {

extern bool g_debug_mode_active;
extern bool g_show_node_tree;
extern std::shared_ptr<Node> g_debug_hovered_node;

class DebugVisualizer {
public:
    DebugVisualizer();
    ~DebugVisualizer() = default;

    bool is_debug_active() const { return g_debug_mode_active; }
    bool is_node_tree_visible() const { return g_show_node_tree; }
    std::shared_ptr<Node> get_debug_hovered_node() const { return g_debug_hovered_node; }

    void set_debug_active(bool active);
    void set_node_tree_visible(bool visible) { g_show_node_tree = visible; }
    void set_debug_hovered_node(std::shared_ptr<Node> node) { g_debug_hovered_node = node; }
    void toggle_node_tree();
};

} // namespace linweb
