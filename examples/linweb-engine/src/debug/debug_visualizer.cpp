#include "debug_visualizer.h"
#include "../renderer/renderer.h"

namespace linweb {

DebugVisualizer::DebugVisualizer() {}

void DebugVisualizer::set_debug_active(bool active) {
    g_debug_mode_active = active;
    if (!active) {
        g_debug_hovered_node = nullptr;
        Renderer::set_debug_box(nullptr);
    }
}

void DebugVisualizer::toggle_node_tree() {
    g_show_node_tree = !g_show_node_tree;
}

} // namespace linweb
