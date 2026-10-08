#include "node_tree_view.h"
#include "../renderer/renderer.h"
#include "../core/dom.h"
#include "debug_visualizer.h"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace linweb {

extern bool g_debug_mode_active;
extern std::shared_ptr<Node> g_debug_hovered_node;

void update_node_tree_hover(const std::shared_ptr<LayoutBox>& box, float& y_offset, int depth, float mx, float my, std::shared_ptr<Node>& hovered_node) {
    if (!box || !box->styled_node || !box->styled_node->node) return;
    if (box->styled_node->node->type() == NodeType::Element && box->styled_node->node->tag_name == "br") return;

    float x_indent = 20.0f * depth;
    float font_size = 14.0f;
    float line_height = 18.0f;

    std::string label;
    if (box->styled_node->node->type() == NodeType::Element) {
        auto el = std::static_pointer_cast<ElementNode>(box->styled_node->node);
        label = "<" + el->tag_name + ">";
        if (el->data.attributes.count("id")) {
            label += " #" + el->data.attributes.at("id");
        }
        if (!el->data.classes().empty()) {
            label += " .";
            for (const auto& cls : el->data.classes()) label += cls + " ";
        }
    } else {
        label = "#text";
        auto text_node = std::static_pointer_cast<TextNode>(box->styled_node->node);
        std::string content = text_node->data;
        if (content.length() > 20) content = content.substr(0, 17) + "...";
        label += " \"" + content + "\"";
    }

    float text_width = Renderer::measure_text(label, font_size);
    float item_x = 10.0f + x_indent;
    float item_y = y_offset - line_height * 0.8f;
    float item_w = text_width + 10.0f;
    float item_h = line_height;

    if (mx >= item_x && mx <= item_x + item_w && my >= item_y && my <= item_y + item_h) {
        hovered_node = box->styled_node->node;
    }

    y_offset += line_height;
    for (const auto& child : box->children) {
        update_node_tree_hover(child, y_offset, depth + 1, mx, my, hovered_node);
    }
}

void render_node_tree(const std::shared_ptr<LayoutBox>& box, float& y_offset, int depth, float mx, float my) {
    if (!box || !box->styled_node || !box->styled_node->node) return;
    if (box->styled_node->node->type() == NodeType::Element && box->styled_node->node->tag_name == "br") return;

    float x_indent = 20.0f * depth;
    float font_size = 14.0f;
    float line_height = 18.0f;

    std::string label;
    if (box->styled_node->node->type() == NodeType::Element) {
        auto el = std::static_pointer_cast<ElementNode>(box->styled_node->node);
        label = "<" + el->tag_name + ">";
        if (el->data.attributes.count("id")) {
            label += " #" + el->data.attributes.at("id");
        }
        if (!el->data.classes().empty()) {
            label += " .";
            for (const auto& cls : el->data.classes()) label += cls + " ";
        }
    } else {
        label = "#text";
        auto text_node = std::static_pointer_cast<TextNode>(box->styled_node->node);
        std::string content = text_node->data;
        if (content.length() > 20) content = content.substr(0, 17) + "...";
        label += " \"" + content + "\"";
    }

    float text_width = Renderer::measure_text(label, font_size);
    float item_x = 10.0f + x_indent;
    float item_y = y_offset - line_height * 0.8f;
    float item_w = text_width + 10.0f;
    float item_h = line_height;

    // Check hover
    bool is_hovered = (mx >= item_x && mx <= item_x + item_w && my >= item_y && my <= item_y + item_h);
    if (is_hovered && g_debug_mode_active) {
        g_debug_hovered_node = box->styled_node->node;
        Renderer::set_debug_box(box);
    }

    // Draw background for readability
    float bg_alpha = is_hovered ? 0.8f : 0.6f;
    Renderer::draw_rect(item_x, item_y, item_w, item_h, 0.0f, 0.0f, 0.0f, bg_alpha, 4.0f);
    
    // Draw text
    float tr = 1.0f, tg = 1.0f, tb = 1.0f;
    if (is_hovered) { tr = 1.0f; tg = 1.0f; tb = 0.0f; } // Yellow on hover
    Renderer::draw_text(label, 15.0f + x_indent, y_offset, font_size, tr, tg, tb, 1.0f);

    if(g_debug_mode_active) {
        Renderer::draw_text("Debug", 10, 400, font_size, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    
    y_offset += line_height;

    for (const auto& child : box->children) {
        render_node_tree(child, y_offset, depth + 1, mx, my);
    }
}

static std::string fmt_f(float v) {
    if (std::fabs(v - std::round(v)) < 0.05f) return std::to_string((int)std::round(v));
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << v;
    return ss.str();
}

static std::string build_node_label(const std::shared_ptr<LayoutBox>& box) {
    if (!box || !box->styled_node || !box->styled_node->node) return "";

    std::string label;
    if (box->styled_node->node->type() == NodeType::Element) {
        auto el = std::static_pointer_cast<ElementNode>(box->styled_node->node);
        label = "<" + el->tag_name + ">";
        if (el->data.attributes.count("id")) {
            label += " #" + el->data.attributes.at("id");
        }
        if (!el->data.classes().empty()) {
            label += " .";
            for (const auto& cls : el->data.classes()) label += cls + " ";
        }
    } else {
        label = "#text";
        auto text_node = std::static_pointer_cast<TextNode>(box->styled_node->node);
        std::string content = text_node->data;
        if (content.length() > 15) content = content.substr(0, 12) + "...";
        label += " \"" + content + "\"";
    }
    return label;
}

static std::string build_dimens_label(const std::shared_ptr<LayoutBox>& box) {
    const auto& d = box->dimensions;
    float total_w = d.content.width + d.padding.left + d.padding.right + d.border.left + d.border.right + d.margin.left + d.margin.right;
    float total_h = d.content.height + d.padding.top + d.padding.bottom + d.border.top + d.border.bottom + d.margin.top + d.margin.bottom;

    std::string label = build_node_label(box);
    label += "  T:" + fmt_f(total_w) + "x" + fmt_f(total_h);
    label += " C:" + fmt_f(d.content.width) + "x" + fmt_f(d.content.height);
    label += " M:" + fmt_f(d.margin.top) + "/" + fmt_f(d.margin.bottom) + "/" + fmt_f(d.margin.left) + "/" + fmt_f(d.margin.right);
    label += " P:" + fmt_f(d.padding.top) + "/" + fmt_f(d.padding.bottom) + "/" + fmt_f(d.padding.left) + "/" + fmt_f(d.padding.right);
    return label;
}

void update_dimens_tree_hover(const std::shared_ptr<LayoutBox>& box, float& y_offset, int depth, float mx, float my, std::shared_ptr<Node>& hovered_node) {
    if (!box || !box->styled_node || !box->styled_node->node) return;
    if (box->styled_node->node->type() == NodeType::Element && box->styled_node->node->tag_name == "br") return;

    float x_indent = 20.0f * depth;
    float font_size = 14.0f;
    float line_height = 18.0f;

    std::string label = build_dimens_label(box);
    float text_width = Renderer::measure_text(label, font_size);
    float item_x = 10.0f + x_indent;
    float item_y = y_offset - line_height * 0.8f;
    float item_w = text_width + 10.0f;
    float item_h = line_height;

    if (mx >= item_x && mx <= item_x + item_w && my >= item_y && my <= item_y + item_h) {
        hovered_node = box->styled_node->node;
    }

    y_offset += line_height;
    for (const auto& child : box->children) {
        update_dimens_tree_hover(child, y_offset, depth + 1, mx, my, hovered_node);
    }
}

void render_dimens_tree(const std::shared_ptr<LayoutBox>& box, float& y_offset, int depth, float mx, float my) {
    if (!box || !box->styled_node || !box->styled_node->node) return;
    if (box->styled_node->node->type() == NodeType::Element && box->styled_node->node->tag_name == "br") return;

    float x_indent = 20.0f * depth;
    float font_size = 14.0f;
    float line_height = 18.0f;

    std::string label = build_dimens_label(box);
    float text_width = Renderer::measure_text(label, font_size);
    float item_x = 10.0f + x_indent;
    float item_y = y_offset - line_height * 0.8f;
    float item_w = text_width + 10.0f;
    float item_h = line_height;

    bool is_hovered = (mx >= item_x && mx <= item_x + item_w && my >= item_y && my <= item_y + item_h);
    if (is_hovered && g_debug_mode_active) {
        g_debug_hovered_node = box->styled_node->node;
        Renderer::set_debug_box(box);
    }

    float bg_alpha = is_hovered ? 0.8f : 0.6f;
    Renderer::draw_rect(item_x, item_y, item_w, item_h, 0.0f, 0.0f, 0.0f, bg_alpha, 4.0f);

    float tr = 1.0f, tg = 1.0f, tb = 1.0f;
    if (is_hovered) { tr = 1.0f; tg = 1.0f; tb = 0.0f; }
    Renderer::draw_text(label, 15.0f + x_indent, y_offset, font_size, tr, tg, tb, 1.0f);

    y_offset += line_height;
    for (const auto& child : box->children) {
        render_dimens_tree(child, y_offset, depth + 1, mx, my);
    }
}

} // namespace linweb
