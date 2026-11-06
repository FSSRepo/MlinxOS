#include "layout/layout_engine.h"
#include "renderer/renderer.h"
#include "forms/form_controls.h"
#include "utils/control_label.h"
#include <algorithm>
#include <functional>

namespace linweb {
namespace {
float preferred(const std::shared_ptr<LayoutBox>& box) {
    const auto& style = box->styled_node;
    float font = LayoutEngine::parse_px(style->value("font-size"), 16);
    if (font <= 0) font = 16;
    if (style->node->is_text()) return Renderer::measure_text(std::static_pointer_cast<TextNode>(style->node)->data, font);
    float content = 0, line = 0;
    for (const auto& child : box->children) {
        if (child->styled_node->node->tag_name == "br") { content = std::max(content, line); line = 0; continue; }
        float width = preferred(child);
        if (child->box_type == BoxType::Inline || child->box_type == BoxType::InlineBlock) line += width;
        else { content = std::max(content, std::max(line, width)); line = 0; }
    }
    content = std::max(content, line);
    std::string width = style->value("width");
    if (!width.empty() && width.find('%') == std::string::npos && width != "auto") content = LayoutEngine::parse_px(width, font);
    else if (is_button_input(*style->node)) content = Renderer::measure_text(button_input_label(*style->node), font);
    else if (forms::kind_of(*style->node) == forms::Kind::Text) {
        auto size = style->node->data.attributes.find("size");
        int chars = size == style->node->data.attributes.end() ? 20 : std::max(1, std::atoi(size->second.c_str()));
        content = std::min(chars, 2048) * Renderer::measure_text("0", font);
    }
    // Resolve the edges through the same box model used by regular layout.
    box->resolve_margin_padding_border(800, 600, font);
    return content + box->dimensions.padding.left + box->dimensions.padding.right + box->dimensions.border.left + box->dimensions.border.right +
        std::max(0.0f, box->dimensions.margin.left) + std::max(0.0f, box->dimensions.margin.right);
}
}
void LayoutBox::layout_table(Dimensions container) {
    std::vector<std::shared_ptr<LayoutBox>> rows;
    for (const auto& child : children) {
        if (child->styled_node->node->tag_name == "tr") rows.push_back(child);
        else for (const auto& row : child->children) if (row->styled_node->node->tag_name == "tr") rows.push_back(row);
    }
    std::vector<float> widths;
    for (const auto& row : rows) {
        if (widths.size() < row->children.size()) widths.resize(row->children.size());
        for (size_t col = 0; col < row->children.size(); ++col) widths[col] = std::max(widths[col], preferred(row->children[col]));
    }
    // Empty percentage columns are often used as symmetric spacers in legacy
    // HTML. Give equal percentage hints equal minimum widths.
    for (const auto& row : rows) for (size_t col = 0; col < row->children.size(); ++col) {
        const auto& attrs = row->children[col]->styled_node->node->data.attributes;
        auto hint = attrs.find("width");
        if (hint == attrs.end() || hint->second.find('%') == std::string::npos) continue;
        for (size_t other = 0; other < row->children.size(); ++other) {
            const auto& other_attrs = row->children[other]->styled_node->node->data.attributes;
            auto match = other_attrs.find("width");
            if (match != other_attrs.end() && hint->second == match->second) widths[col] = std::max(widths[col], widths[other]);
        }
    }
    float spacing = 2;
    auto attrs = styled_node->node->data.attributes;
    if (attrs.count("cellspacing")) spacing = std::max(0.0f, LayoutEngine::parse_px(attrs["cellspacing"], 0));
    float natural = spacing * (widths.size() + 1);
    for (float width : widths) natural += width;
    float table_width = natural;
    std::string specified_width = styled_node->value("width");
    if (!specified_width.empty() && specified_width != "auto") table_width = std::max(natural, dimensions.content.width);
    // Percentage column hints contribute to the table's preferred width.
    // Preserve intrinsic widths when the available space is too small.
    std::vector<float> percentages(widths.size(), 0.0f);
    for (const auto& row : rows) for (size_t col = 0; col < row->children.size(); ++col) {
        const auto& attributes = row->children[col]->styled_node->node->data.attributes;
        auto hint = attributes.find("width");
        if (hint != attributes.end() && hint->second.find('%') != std::string::npos)
            percentages[col] = std::max(percentages[col], std::max(0.0f, LayoutEngine::parse_px(hint->second, 1.0f)));
    }
    float fraction = 0, unconstrained = 0;
    for (size_t col = 0; col < widths.size(); ++col) {
        fraction += percentages[col];
        if (percentages[col] == 0) unconstrained += widths[col];
    }
    if (specified_width.empty() && fraction > 0 && fraction < 1 && unconstrained > 0) {
        float preferred_width = spacing * (widths.size() + 1) + unconstrained / (1 - fraction);
        table_width = std::max(natural, std::min(container.content.width, preferred_width));
    }
    if (table_width > natural && !widths.empty()) {
        float extra = table_width - natural;
        for (size_t col = 0; col < widths.size(); ++col)
            widths[col] += extra * (fraction > 0 ? percentages[col] / fraction : 1.0f / widths.size());
    }
    dimensions.content.width = table_width;
    if (styled_node->value("text-align") == "center" && table_width < container.content.width)
        dimensions.content.x += (container.content.width - table_width) / 2;
    float y = dimensions.content.y + spacing;
    for (const auto& row : rows) {
        float x = dimensions.content.x + spacing, height = 0;
        for (size_t col = 0; col < row->children.size(); ++col) {
            const auto& cell = row->children[col];
            auto saved = cell->styled_node->specified_values;
            cell->styled_node->specified_values["width"] = std::to_string(widths[col]) + "px";
            cell->styled_node->specified_values["box-sizing"] = "border-box";
            cell->styled_node->specified_values["margin"] = "0";
            Dimensions cell_container;
            cell_container.content = {x, y, widths[col], container.content.height};
            cell->layout(cell_container);
            height = std::max(height, cell->dimensions.content.height + cell->dimensions.padding.top + cell->dimensions.padding.bottom + cell->dimensions.border.top + cell->dimensions.border.bottom);
            cell->styled_node->specified_values = std::move(saved);
            x += widths[col] + spacing;
        }
        row->dimensions.content = {dimensions.content.x, y, table_width, height};
        row->total_content_height = height;
        y += height + spacing;
    }
    dimensions.content.height = y - dimensions.content.y;
    total_content_height = dimensions.content.height;
    total_y_offset = 0;
    last_line_width = table_width;
    for (const auto& group : children) if (group->styled_node->node->tag_name != "tr") {
        group->dimensions.content = dimensions.content;
        group->total_content_height = dimensions.content.height;
    }
}
}
