#include "debug/anim_panel.h"
#include <cstdio>
#include <string>
#include "renderer/renderer.h"
#include "style/animation_engine.h"

namespace linweb {

void draw_animation_panel()
{
    if (g_anim_debug_entries.empty()) return;
    float panel_x = 10.0f, panel_y = 10.0f;
    float line_h = 18.0f;
    float panel_w = 800.0f;
    int total_lines = 1;
    for (const auto& entry : g_anim_debug_entries) {
        total_lines += 3;
        total_lines += (int)entry.animated_props.size();
    }
    float panel_h = total_lines * line_h + 10.0f;
    Renderer::draw_rect(panel_x, panel_y, panel_w, panel_h, 0.0f, 0.0f, 0.0f, 0.75f, 6.0f);

    float text_x = panel_x + 8.0f;
    float text_y = panel_y + 6.0f;
    Renderer::draw_text("=== ANIMATIONS (" + std::to_string(g_anim_debug_entries.size()) + " active) ===", text_x, text_y, 14.0f, 0.3f, 0.8f, 1.0f, 1.0f, true);
    text_y += line_h;

    for (const auto& entry : g_anim_debug_entries) {
        std::string node_label = "<" + entry.node_tag + ">";
        if (!entry.node_id.empty()) node_label += " #" + entry.node_id;

        float pct = entry.t * 100.0f;
        float eased_pct = entry.eased_t * 100.0f;
        char time_buf[64];
        snprintf(time_buf, sizeof(time_buf), "%.2fs", entry.elapsed);

        Renderer::draw_text(node_label, text_x, text_y, 14.0f, 1.0f, 1.0f, 1.0f, 1.0f, true);
        text_y += line_h;

        std::string progress = std::string("  [") + entry.anim_name + "] " + time_buf + "  linear: " +
            std::to_string((int)pct) + "%  eased: " + std::to_string((int)eased_pct) + "%  (" + entry.timing_function + ")";
        Renderer::draw_text(progress, text_x, text_y, 13.0f, 0.6f, 0.9f, 0.6f, 1.0f);
        text_y += line_h;

        for (const auto& [prop, val] : entry.animated_props) {
            std::string prop_line = "    " + prop + ": " + val.actual + "  (start: " + val.start + ", end: " + val.end + ")";
            Renderer::draw_text(prop_line, text_x, text_y, 13.0f, 0.8f, 0.8f, 0.8f, 1.0f, false, 0.0f, 0.0f, false);
            text_y += line_h;
        }
    }
}

} // namespace linweb
