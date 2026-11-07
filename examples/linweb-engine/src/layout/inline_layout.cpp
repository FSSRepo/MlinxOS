#include "renderer/custom_font.h"
#include "layout_engine.h"
#include "utils/text_transform.h"
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
            // A break on an empty line still advances by the parent's line height.
            current_y_offset += max_line_height > 0 ? max_line_height : line_height;
            max_line_height = 0;
            child->dimensions.content.width = 0;
            child->dimensions.content.height = 0;
            return;
        }

        if (child->box_type == BoxType::Inline && child->styled_node->node->type() == NodeType::Element && child->is_empty_inline())
        {
            Dimensions here;
            here.content.x = dimensions.content.x + current_x_offset;
            here.content.y = dimensions.content.y + current_y_offset;
            child->layout(here);
            child->dimensions.content.height = 0;
            child->dimensions.content.width = 0;
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
            const CustomFont* custom = CustomFonts::select(font_family, font_weight, child->styled_node->value("font-style"));

            const std::string shown = apply_text_transform(text_node->data, child->styled_node->value("text-transform"));
            child_width = Renderer::measure_text(shown, font_size, is_bold, is_monospace, custom)
                + parse_letter_spacing(child->styled_node->value("letter-spacing")) * static_cast<float>(utf8_char_count(shown));
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

        // A text that does not fit what is left of the line fills it with the words that fit and goes on below (its own
        // wrapping does that); only when not even its first word fits does it start a new line.
        bool fills_rest_of_line = false;
        std::shared_ptr<LayoutBox> first_text = child;
        while (first_text && first_text->styled_node->node->type() != NodeType::Text && first_text->box_type == BoxType::Inline &&
               !first_text->children.empty())
            first_text = first_text->children.front();
        if (first_text && first_text->styled_node->node->type() == NodeType::Text && first_text->styled_node->value("white-space") != "nowrap" &&
            first_text->styled_node->value("white-space").compare(0, 3, "pre") != 0)
        {
            const std::string shown_text = first_text->text_to_show();
            const size_t first_char = shown_text.find_first_not_of(' ');
            const size_t first_space = first_char == std::string::npos ? std::string::npos : shown_text.find(' ', first_char);
            if (first_space != std::string::npos && first_space + 1 < shown_text.size())
                fills_rest_of_line = first_text->measure_text_run(shown_text.substr(0, first_space)) <= available_width - current_x_offset;
        }

        if (styled_node->value("white-space") != "nowrap" && !fills_rest_of_line && current_x_offset + child_width > available_width && current_x_offset > 0)
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
            // the padding, border and margin above and below an inline box do not make its line taller; an inline-block's do
            const bool grows_line = child->box_type != BoxType::Inline;
            max_line_height = std::max(max_line_height, child->dimensions.content.height +
                                                            (grows_line ? child->dimensions.padding.top + child->dimensions.padding.bottom +
                                                                          child->dimensions.border.top + child->dimensions.border.bottom +
                                                                          child->dimensions.margin.top + child->dimensions.margin.bottom : 0.0f));
            max_x_offset = std::max(max_x_offset, current_x_offset);
        }
    }

} // namespace linweb
