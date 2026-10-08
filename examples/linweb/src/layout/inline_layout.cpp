#include "layout_engine.h"
#include "../renderer/renderer.h"
#include <vector>
#include <memory>
#include <string>

namespace linweb
{

    /**
     * @brief Lays out an inline or inline-block child.
     *
     * @param child The child to position.
     * @param container_dimensions Container dimensions.
     * @param current_x_offset Current X offset on the line.
     * @param current_y_offset Accumulated Y offset.
     * @param max_line_height Maximum height of the current line.
     * @param max_x_offset Maximum width reached.
     * @param current_line Vector of elements on the current line.
     * @param line_height Line height to apply.
     */
    void LayoutBox::layout_inline_child(const std::shared_ptr<LayoutBox> &child, Dimensions container_dimensions, float &current_x_offset, float &current_y_offset, float &max_line_height, float &max_x_offset, std::vector<std::shared_ptr<LayoutBox>> &current_line, float line_height)
    {
        if (child->styled_node->node->type() == NodeType::Element && child->styled_node->node->tag_name == "br") {
            apply_line_alignment(current_line, current_x_offset);
            current_line.clear();
            max_x_offset = std::max(max_x_offset, current_x_offset);
            current_x_offset = 0;
            current_y_offset += max_line_height;
            max_line_height = 0;
            child->dimensions.content.width = 0;
            child->dimensions.content.height = 0;
            return;
        }

        float available_width = dimensions.content.width;
        if (box_type == BoxType::Inline || box_type == BoxType::InlineBlock)
        {
            available_width = container_dimensions.content.width - (dimensions.content.x - container_dimensions.content.x);
        }
        if (available_width <= 0.001f) {
            available_width = container_dimensions.content.width;
        }

        float child_width = 0;
        if (child->styled_node->node->type() == NodeType::Text)
        {
            auto text_node = std::static_pointer_cast<TextNode>(child->styled_node->node);
            float font_size = LayoutEngine::parse_px(child->styled_node->value("font-size"), 16.0f);
            if (font_size <= 0)
                font_size = 16.0f;

            std::string font_weight = child->styled_node->value("font-weight");
            bool is_bold = (font_weight == "bold" || font_weight == "700" || font_weight == "800" || font_weight == "900");
            std::string font_family = child->styled_node->value("font-family");
            bool is_monospace = (font_family == "monospace");

            child_width = Renderer::measure_text(text_node->data, font_size, is_bold, is_monospace);
            float child_height = line_height;

            if (text_node->tag_name != "#text")
            {
                child_width += LayoutEngine::parse_px(child->styled_node->value("margin-left"), available_width) +
                               LayoutEngine::parse_px(child->styled_node->value("margin-right"), available_width) +
                               LayoutEngine::parse_px(child->styled_node->value("padding-left"), available_width) +
                               LayoutEngine::parse_px(child->styled_node->value("padding-right"), available_width) +
                               LayoutEngine::parse_px(child->styled_node->value("border-left-width"), available_width) +
                               LayoutEngine::parse_px(child->styled_node->value("border-right-width"), available_width);
            }

            child->dimensions.content.width = child_width;
            child->dimensions.content.height = child_height;
        }
        else
        {
            std::string child_w_str = child->styled_node->value("width");
            if (!child_w_str.empty())
            {
                child_width = LayoutEngine::parse_px(child_w_str, available_width);
            }
            else if (child->box_type == BoxType::Inline)
            {
                child->layout({Dimensions()}); // Temporary layout to get width
                child_width = child->dimensions.content.width;
            }

            child_width += LayoutEngine::parse_px(child->styled_node->value("margin-left"), available_width) +
                           LayoutEngine::parse_px(child->styled_node->value("margin-right"), available_width) +
                           LayoutEngine::parse_px(child->styled_node->value("padding-left"), available_width) +
                           LayoutEngine::parse_px(child->styled_node->value("padding-right"), available_width) +
                           LayoutEngine::parse_px(child->styled_node->value("border-left-width"), available_width) +
                           LayoutEngine::parse_px(child->styled_node->value("border-right-width"), available_width);
        }

        Dimensions child_container;
        child_container.content.x = dimensions.content.x + current_x_offset;
        child_container.content.y = dimensions.content.y + current_y_offset;
        child_container.content.width = available_width - current_x_offset;
        child_container.content.height = container_dimensions.content.height;

        if (current_x_offset + child_width > available_width && current_x_offset > 0)
        {
            apply_line_alignment(current_line, current_x_offset);
            current_line.clear();

            max_x_offset = std::max(max_x_offset, current_x_offset);
            current_x_offset = 0;
            current_y_offset += max_line_height;
            max_line_height = 0;

            child_container.content.x = dimensions.content.x + current_x_offset;
            child_container.content.y = dimensions.content.y + current_y_offset;
            child_container.content.width = available_width - current_x_offset;
        }

        child->layout(child_container);
        current_line.push_back(child);

        if (child->box_type == BoxType::Inline && child->total_y_offset > 0)
        {
            apply_line_alignment(current_line, available_width);
            current_line.clear();

            max_x_offset = std::max(max_x_offset, available_width);
            current_y_offset += child->total_y_offset;
            current_x_offset = child->last_line_width + child->dimensions.margin.left + child->dimensions.margin.right +
                               child->dimensions.padding.left + child->dimensions.padding.right +
                               child->dimensions.border.left + child->dimensions.border.right;
            max_line_height = (child->dimensions.content.height - child->total_y_offset) +
                              child->dimensions.padding.top + child->dimensions.padding.bottom +
                              child->dimensions.border.top + child->dimensions.border.bottom +
                              child->dimensions.margin.top + child->dimensions.margin.bottom;

            current_line.push_back(child);
        }
        else
        {
            current_x_offset += child->dimensions.content.width + child->dimensions.margin.left + child->dimensions.margin.right +
                                child->dimensions.padding.left + child->dimensions.padding.right +
                                child->dimensions.border.left + child->dimensions.border.right;
            max_line_height = std::max(max_line_height, child->dimensions.content.height +
                                                            child->dimensions.padding.top + child->dimensions.padding.bottom +
                                                            child->dimensions.border.top + child->dimensions.border.bottom +
                                                            child->dimensions.margin.top + child->dimensions.margin.bottom);
            max_x_offset = std::max(max_x_offset, current_x_offset);
        }
    }

} // namespace linweb
