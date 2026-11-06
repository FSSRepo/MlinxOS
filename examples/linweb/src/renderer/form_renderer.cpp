#include "renderer/form_renderer.h"
#include "renderer/renderer.h"
#include "renderer/renderer_internal.h"
#include "forms/form_controls.h"
#include "style/colors.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace linweb {

namespace {

Color color_property(const LayoutBox& box, const char* name, Color fallback) {
    std::string value = box.styled_node->value(name);
    return value.empty() ? fallback : parse_color_str(value);
}

void rect(float x, float y, float w, float h, const Color& c, float opacity, float radius = 0.0f, float border = 0.0f) {
    if (w <= 0 || h <= 0 || c.a <= 0) return;
    Renderer::draw_rect(x, y, w, h, c.r, c.g, c.b, c.a * opacity, radius, 0.0f, 0.0f, Gradient(), border);
}

// Ring around the border box of the focused control.
void focus_ring(const LayoutBox& box, const Color& accent, float opacity, float radius) {
    const Dimensions& d = box.dimensions;
    float x = d.content.x - d.padding.left - d.border.left - 1.0f;
    float y = d.content.y - d.padding.top - d.border.top - 1.0f;
    float w = d.content.width + d.padding.left + d.padding.right + d.border.left + d.border.right + 2.0f;
    float h = d.content.height + d.padding.top + d.padding.bottom + d.border.top + d.border.bottom + 2.0f;
    rect(x, y, w, h, accent, 0.9f * opacity, radius + 1.0f, 2.0f);
}

void draw_text_input(const std::shared_ptr<LayoutBox>& box, const std::shared_ptr<Node>& node, float opacity, float radius) {
    const Rect& c = box->dimensions.content;
    float font_size = LayoutEngine::parse_px(box->styled_node->value("font-size"), 16.0f);
    if (font_size <= 0) font_size = 16.0f;
    Color text = color_property(*box, "color", {0, 0, 0, 1});
    Color caret = color_property(*box, "caret-color", text);
    Color accent = color_property(*box, "accent-color", {0.23f, 0.51f, 0.96f, 1.0f});

    forms::TextView view = forms::text_view(node, c.width, font_size);
    float scale = font_size / RendererState::base_font_size;
    float line_h = Renderer::get_line_height(font_size);
    float top = c.y + (c.height - line_h) / 2.0f;
    float baseline = top + RendererState::font_ascent_px * scale;

    // the placeholder of a password field is plain text; only typed characters are masked
    if (view.password && !view.placeholder) {
        // a dot per character instead of a text of asterisks
        float advance = Renderer::measure_text("*", font_size);
        float diameter = font_size * 0.30f;   // smaller than the advance so the dots keep a gap
        for (size_t i = 0; i < view.text.size(); ++i) {
            float cx = c.x + (static_cast<float>(i) + 0.5f) * advance;
            rect(cx - diameter / 2.0f, top + line_h / 2.0f - diameter / 2.0f, diameter, diameter, text, opacity, diameter / 2.0f);
        }
    } else if (!view.text.empty()) {
        Color shown = view.placeholder ? Color{text.r, text.g, text.b, text.a * 0.45f} : text;
        Renderer::draw_text(view.text, c.x, baseline, font_size, shown.r, shown.g, shown.b, shown.a * opacity);
    }
    if (view.show_caret) rect(c.x + view.caret_x, top, 1.5f, line_h, caret, opacity);
    if (forms::focused() == node) focus_ring(*box, accent, opacity, radius);
}

// The chosen option in the box, an arrow on the right, and the list while it is open (drawn after the box, so a
// later sibling can cover it).
void draw_select(const std::shared_ptr<LayoutBox>& box, const std::shared_ptr<Node>& node, float opacity, float radius) {
    const Rect& c = box->dimensions.content;
    float font_size = LayoutEngine::parse_px(box->styled_node->value("font-size"), 13.3f);
    if (font_size <= 0) font_size = 13.3f;
    Color text = color_property(*box, "color", {0, 0, 0, 1});
    Color accent = color_property(*box, "accent-color", {0.23f, 0.51f, 0.96f, 1.0f});
    float scale = font_size / RendererState::base_font_size;
    float line_h = Renderer::get_line_height(font_size);
    float top = c.y + (c.height - line_h) / 2.0f;
    float baseline = top + RendererState::font_ascent_px * scale;

    forms::Rect content{c.x, c.y, c.width, c.height};
    forms::remember_box(node, content, font_size);
    std::vector<std::shared_ptr<Node>> options = forms::options_of(*node);
    int chosen = forms::selected_index(*node);
    if (chosen >= 0) {
        Renderer::draw_text(forms::option_text(*options[chosen]), c.x, baseline, font_size, text.r, text.g, text.b, text.a * opacity);
    }

    // the arrow: a small down-pointing triangle, centred in the padding on the right that the style reserves for it
    float right_padding = box->dimensions.padding.right;
    float cx = c.x + c.width + (right_padding > 0 ? right_padding / 2.0f : 12.0f);
    float cy = c.y + c.height / 2.0f - 3.0f;
    const float row_h = 1.5f;   // rows touch each other, so the arrow is one solid shape
    for (int row = 0; row < 4; ++row) {
        float w = 8.0f - 2.0f * static_cast<float>(row);
        rect(cx - w / 2.0f, cy + row_h * static_cast<float>(row), w, row_h, text, opacity);
    }

    if (forms::focused() == node) focus_ring(*box, accent, opacity, radius);
}

// The list of an open select, drawn over the whole page: a box's clipping and stacking must not cut it.
void draw_select_popup(const std::shared_ptr<LayoutBox>& box, const std::shared_ptr<Node>& node) {
    const Rect& c = box->dimensions.content;
    float font_size = LayoutEngine::parse_px(box->styled_node->value("font-size"), 13.3f);
    if (font_size <= 0) font_size = 13.3f;
    Color text = color_property(*box, "color", {0, 0, 0, 1});
    Color accent = color_property(*box, "accent-color", {0.23f, 0.51f, 0.96f, 1.0f});
    float scale = font_size / RendererState::base_font_size;
    float line_h = Renderer::get_line_height(font_size);
    std::vector<std::shared_ptr<Node>> options = forms::options_of(*node);

    forms::Rect area = forms::popup_area({c.x, c.y, c.width, c.height}, options.size(), font_size);
    float row_h = forms::popup_row_height(font_size);
    rect(area.x - 1.0f, area.y - 1.0f, area.w + 2.0f, area.h + 2.0f, {0.46f, 0.46f, 0.46f, 1.0f}, 1.0f, 2.0f);
    rect(area.x, area.y, area.w, area.h, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
    int highlight = forms::popup_highlight();
    for (size_t i = 0; i < options.size(); ++i) {
        float y = area.y + 1.0f + static_cast<float>(i) * row_h;
        bool active = static_cast<int>(i) == highlight;
        Color ink = active ? Color{1.0f, 1.0f, 1.0f, 1.0f} : text;
        if (active) rect(area.x + 1.0f, y, area.w - 2.0f, row_h, accent, 1.0f);
        float row_baseline = y + (row_h - line_h) / 2.0f + RendererState::font_ascent_px * scale;
        Renderer::draw_text(forms::option_text(*options[i]), area.x + 6.0f, row_baseline, font_size, ink.r, ink.g, ink.b, ink.a);
    }
}

std::shared_ptr<LayoutBox> find_box(const std::shared_ptr<LayoutBox>& box, const std::shared_ptr<Node>& node) {
    if (!box || !box->styled_node) return nullptr;
    if (box->styled_node->node == node) return box;
    for (const auto& child : box->children) {
        if (auto found = find_box(child, node)) return found;
    }
    return nullptr;
}

void draw_range(const std::shared_ptr<LayoutBox>& box, const std::shared_ptr<Node>& node, float opacity) {
    const Rect& c = box->dimensions.content;
    Color thumb = color_property(*box, "thumb-color", {1, 1, 1, 1});
    Color accent = color_property(*box, "accent-color", {0.23f, 0.51f, 0.96f, 1.0f});
    Color track = color_property(*box, "track-color", {0.78f, 0.78f, 0.78f, 1.0f});

    forms::Rect area{c.x, c.y, c.width, c.height};
    forms::RangeGeometry g = forms::range_geometry(area, forms::range_fraction(*node));
    float round = g.track.h / 2.0f;
    rect(g.track.x, g.track.y, g.track.w, g.track.h, track, opacity, round);
    rect(g.fill.x, g.fill.y, g.fill.w, g.fill.h, accent, opacity, round);

    // thumb: a dark outline so it shows on light and dark backgrounds, then the thumb itself
    float r = g.thumb_r;
    rect(g.thumb_x - r - 1.0f, g.thumb_y - r - 1.0f, 2.0f * r + 2.0f, 2.0f * r + 2.0f, {0, 0, 0, 0.35f}, opacity, r + 1.0f);
    rect(g.thumb_x - r, g.thumb_y - r, 2.0f * r, 2.0f * r, thumb, opacity, r);
    if (forms::focused() == node) rect(g.thumb_x - r - 2.0f, g.thumb_y - r - 2.0f, 2.0f * r + 4.0f, 2.0f * r + 4.0f, accent, 0.9f * opacity, r + 2.0f, 2.0f);
}

void draw_progress(const std::shared_ptr<LayoutBox>& box, const std::shared_ptr<Node>& node, float opacity) {
    const Rect& c = box->dimensions.content;
    Color accent = color_property(*box, "accent-color", {0.23f, 0.51f, 0.96f, 1.0f});
    Color track = color_property(*box, "track-color", {0.78f, 0.78f, 0.78f, 1.0f});
    float round = c.height / 2.0f;
    rect(c.x, c.y, c.width, c.height, track, opacity, round);

    double fraction = forms::progress_fraction(*node);
    if (fraction >= 0) {
        rect(c.x, c.y, std::max(c.height, static_cast<float>(fraction) * c.width), c.height, accent, opacity, round);
        return;
    }
    // no value: a bar that travels along the track
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    double t = std::fmod(ms / 1200.0, 1.0);
    float bar = c.width * 0.3f;
    float x = c.x + static_cast<float>(t) * (c.width - bar);
    rect(x, c.y, bar, c.height, accent, opacity, round);
}

} // namespace

void draw_form_control(const std::shared_ptr<LayoutBox>& box, float opacity, float radius) {
    if (!box || !box->styled_node || !box->styled_node->node) return;
    const std::shared_ptr<Node>& node = box->styled_node->node;
    switch (forms::kind_of(*node)) {
        case forms::Kind::Text: draw_text_input(box, node, opacity, radius); break;
        case forms::Kind::Range: draw_range(box, node, opacity); break;
        case forms::Kind::Progress: draw_progress(box, node, opacity); break;
        case forms::Kind::Select: draw_select(box, node, opacity, radius); break;
        case forms::Kind::None: break;
    }
}

void draw_form_overlays(const std::shared_ptr<LayoutBox>& root) {
    const std::shared_ptr<Node>& open = forms::popup_node();
    if (!open) return;
    if (auto box = find_box(root, open)) draw_select_popup(box, open);
}

void draw_list_marker(const std::shared_ptr<LayoutBox>& box, float opacity) {
    if (!box || !box->styled_node || !box->styled_node->node) return;
    const std::shared_ptr<Node>& node = box->styled_node->node;
    if (node->tag_name != "li") return;
    std::shared_ptr<Node> list = node->parent.lock();
    if (!list || (list->tag_name != "ul" && list->tag_name != "ol")) return;

    const Rect& c = box->dimensions.content;
    float font_size = LayoutEngine::parse_px(box->styled_node->value("font-size"), 16.0f);
    if (font_size <= 0) font_size = 16.0f;
    Color text = color_property(*box, "color", {0, 0, 0, 1});
    float scale = font_size / RendererState::base_font_size;

    if (list->tag_name == "ol") {
        int number = 1;
        for (const auto& sibling : list->children) {
            if (sibling == node) break;
            if (sibling->tag_name == "li") ++number;
        }
        std::string label = std::to_string(number) + ".";
        float width = Renderer::measure_text(label, font_size);
        float baseline = c.y + RendererState::font_ascent_px * scale;
        Renderer::draw_text(label, c.x - width - font_size * 0.5f, baseline, font_size, text.r, text.g, text.b, text.a * opacity);
    } else {
        // a filled disc, centred on the first line
        float diameter = font_size * 0.35f;
        float x = c.x - font_size * 0.9f;
        float y = c.y + Renderer::get_line_height(font_size) / 2.0f - diameter / 2.0f;
        rect(x, y, diameter, diameter, text, opacity, diameter / 2.0f);
    }
}

} // namespace linweb
