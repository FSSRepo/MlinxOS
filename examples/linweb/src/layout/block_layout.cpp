#include "layout_engine.h"
#include <vector>
#include <memory>
#include <algorithm>
#include <cctype>

namespace linweb
{

    bool is_whitespace_node(const std::shared_ptr<LayoutBox> &box)
    {
        if (box->styled_node->node->type() != NodeType::Text)
            return false;
        auto text_node = std::static_pointer_cast<TextNode>(box->styled_node->node);
        for (char c : text_node->data)
        {
            if (!std::isspace(static_cast<unsigned char>(c)))
                return false;
        }
        return true;
    }

    /**
     * @brief Collapses two vertical margins according to CSS rules.
     *
     * @param a First margin.
     * @param b Second margin.
     * @return float The collapsed margin.
     */
    float LayoutBox::collapse_margins(float a, float b)
    {
        if (a >= 0.0f && b >= 0.0f)
            return std::max(a, b);
        if (a <= 0.0f && b <= 0.0f)
            return std::min(a, b);
        return a + b;
    }

    /**
     * @brief Applies text alignment (left, center, right) to a line of elements.
     *
     * @param line Vector of boxes that make up the line.
     * @param line_width Total width occupied by the elements of the line.
     */
    void LayoutBox::apply_line_alignment(const std::vector<std::shared_ptr<LayoutBox>> &line, float line_width)
    {
        std::string text_align = styled_node->value("text-align");
        if (line.empty() || text_align == "left" || text_align.empty())
            return;

        float extra_space = dimensions.content.width - line_width;
        if (extra_space <= 0)
            return;

        float offset = 0;
        if (text_align == "center")
            offset = extra_space / 2.0f;
        else if (text_align == "right")
            offset = extra_space;

        if (offset > 0)
        {
            for (const auto &box : line)
            {
                box->dimensions.content.x += offset;
                Dimensions box_container;
                box_container.content.x = box->dimensions.content.x - box->dimensions.margin.left - box->dimensions.padding.left - box->dimensions.border.left;
                box_container.content.y = box->dimensions.content.y - box->dimensions.margin.top - box->dimensions.padding.top - box->dimensions.border.top;
                box_container.content.width = box->dimensions.content.width;
                box_container.content.height = box->dimensions.content.height;
                box->layout(box_container);
            }
        }
    }

    /**
     * @brief Lays out a block-type child.
     *
     * @param child The child to position.
     * @param current_x_offset Current X offset.
     * @param current_y_offset Current Y offset.
     * @param max_line_height Line height to reset after a block.
     * @param max_x_offset Maximum width reached.
     * @param current_line Current line to align before the block.
     * @param has_prev_block Indicates whether there was a previous block to collapse margins with.
     * @param pending_prev_block_bottom_margin Bottom margin of the previous block.
     * @param has_any_flow_content Indicates whether there is previous content in the flow.
     * @param parent_can_collapse_top Indicates whether the parent can collapse the top margin.
     * @param parent_can_collapse_bottom Indicates whether the parent can collapse the bottom margin.
     */
    void LayoutBox::layout_block_child(Dimensions container_dimensions, const std::shared_ptr<LayoutBox> &child, float &current_x_offset, float &current_y_offset, float &max_line_height, float &max_x_offset, std::vector<std::shared_ptr<LayoutBox>> &current_line, bool &has_prev_block, float &pending_prev_block_bottom_margin, bool &has_any_flow_content, bool parent_can_collapse_top, bool parent_can_collapse_bottom)
    {
        apply_line_alignment(current_line, current_x_offset);
        current_line.clear();

        if (current_x_offset > 0)
        {
            max_x_offset = std::max(max_x_offset, current_x_offset);
            current_x_offset = 0;
            current_y_offset += max_line_height;
            max_line_height = 0;
        }

        Dimensions child_container;
        child_container.content.x = dimensions.content.x;
        child_container.content.width = dimensions.content.width;
        child_container.content.height = container_dimensions.content.height;

        Dimensions pre_container = child_container;
        pre_container.content.y = 0.0f;
        child->layout(pre_container);

        float child_effective_margin_top = child->dimensions.margin.top;
        if (!has_any_flow_content && parent_can_collapse_top)
        {
            float old_margin = dimensions.margin.top;
            dimensions.margin.top = collapse_margins(dimensions.margin.top, child_effective_margin_top);
            dimensions.content.y += (dimensions.margin.top - old_margin);
            child_effective_margin_top = 0.0f;
        }

        float gap_from_prev = child_effective_margin_top;
        if (has_prev_block)
        {
            gap_from_prev = collapse_margins(pending_prev_block_bottom_margin, child_effective_margin_top);
        }

        child_container.content.y = dimensions.content.y + current_y_offset + gap_from_prev - child->dimensions.margin.top;
        child->layout(child_container);

        float child_layout_height = child->dimensions.content.height +
                                    child->dimensions.padding.top + child->dimensions.padding.bottom +
                                    child->dimensions.border.top + child->dimensions.border.bottom;

        current_y_offset += gap_from_prev + child_layout_height;
        pending_prev_block_bottom_margin = child->dimensions.margin.bottom;
        has_prev_block = true;
        max_x_offset = std::max(max_x_offset, child->dimensions.content.width +
                                                  child->dimensions.padding.left + child->dimensions.padding.right +
                                                  child->dimensions.border.left + child->dimensions.border.right +
                                                  child->dimensions.margin.left + child->dimensions.margin.right);
    }

    /**
     * @brief Manages the normal flow layout (block and inline).
     *
     * @param container_dimensions Container dimensions.
     * @param normal_flow_children Children in the normal flow.
     */
    void LayoutBox::layout_block(Dimensions container_dimensions, const std::vector<std::shared_ptr<LayoutBox>> &normal_flow_children)
    {
        float orig_content_width = dimensions.content.width;

        bool parent_has_inline_content = false;
        for (const auto &c : normal_flow_children)
        {
            if ((c->box_type == BoxType::Inline || c->box_type == BoxType::InlineBlock || c->styled_node->node->type() == NodeType::Text) && !is_whitespace_node(c))
            {
                parent_has_inline_content = true;
                break;
            }
        }

        std::string parent_height_prop = styled_node->value("height");
        std::string parent_min_height_prop = styled_node->value("min-height");
        bool parent_has_specified_height = !parent_height_prop.empty() || !parent_min_height_prop.empty();

        bool is_root = (styled_node->node->tag_name == "html");
        std::string overflow = styled_node->value("overflow");
        bool is_scrollable = (overflow == "auto" || overflow == "scroll" || overflow == "hidden");

        bool parent_can_collapse_top = !is_root && !is_scrollable && (dimensions.border.top == 0.0f && dimensions.padding.top == 0.0f && !parent_has_inline_content);
        bool parent_can_collapse_bottom = !is_root && !is_scrollable && (dimensions.border.bottom == 0.0f && dimensions.padding.bottom == 0.0f && !parent_has_specified_height);

        auto layout_children_pass = [&](float &cur_x, float &cur_y, float &max_lh, float &max_x_off,
                                         std::vector<std::shared_ptr<LayoutBox>> &line,
                                         bool &has_prev, float &pending_margin, bool &has_flow)
        {
            for (const auto &child : normal_flow_children)
            {
                bool is_ws = is_whitespace_node(child);
                if (child->box_type == BoxType::Inline || child->box_type == BoxType::InlineBlock)
                {
                    if (has_prev && !is_ws)
                    {
                        cur_y += pending_margin;
                        pending_margin = 0.0f;
                        has_prev = false;
                    }
                    layout_inline_child(child, container_dimensions, cur_x, cur_y, max_lh, max_x_off, line, this->line_height);
                }
                else
                {
                    layout_block_child(container_dimensions, child, cur_x, cur_y, max_lh, max_x_off, line, has_prev, pending_margin, has_flow, parent_can_collapse_top, parent_can_collapse_bottom);
                }

                if (!is_ws)
                    has_flow = true;
            }
        };

        auto cleanup_line = [&](bool &has_prev, float &pending_margin, std::vector<std::shared_ptr<LayoutBox>> &line, float &cur_y, float &cur_x, float &max_lh, float &max_x_off)
        {
            if (has_prev)
            {
                if (!parent_can_collapse_bottom)
                    cur_y += pending_margin;
                else
                    dimensions.margin.bottom = collapse_margins(dimensions.margin.bottom, pending_margin);
                pending_margin = 0.0f;
                has_prev = false;
            }

            if (box_type != BoxType::Grid)
                apply_line_alignment(line, cur_x);
            line.clear();
        };

        // Save original margin state for potential second pass
        float saved_margin_top = dimensions.margin.top;
        float saved_content_y = dimensions.content.y;

        float max_x_offset = 0;

        // First pass
        {
            float current_x_offset = 0;
            float current_y_offset = 0;
            float max_line_height = 0;
            std::vector<std::shared_ptr<LayoutBox>> current_line;
            bool has_prev_block = false;
            float pending_prev_block_bottom_margin = 0.0f;
            bool has_any_flow_content = false;

            layout_children_pass(current_x_offset, current_y_offset, max_line_height, max_x_offset,
                                 current_line, has_prev_block, pending_prev_block_bottom_margin, has_any_flow_content);

            cleanup_line(has_prev_block, pending_prev_block_bottom_margin, current_line,
                         current_y_offset, current_x_offset, max_line_height, max_x_offset);

            resolve_height(container_dimensions, current_y_offset, max_line_height, max_x_offset, current_x_offset);
        }

        // Second pass: if width was auto-determined from children (shrink-to-fit) and changed
        std::string w_str = styled_node->value("width");
        if (w_str.empty() && max_x_offset > orig_content_width + 0.001f)
        {
            dimensions.content.width = max_x_offset;

            // Restore margin state before second pass
            dimensions.margin.top = saved_margin_top;
            dimensions.content.y = saved_content_y;

            float current_x_offset = 0;
            float current_y_offset = 0;
            float max_line_height = 0;
            std::vector<std::shared_ptr<LayoutBox>> current_line;
            bool has_prev_block = false;
            float pending_prev_block_bottom_margin = 0.0f;
            bool has_any_flow_content = false;

            layout_children_pass(current_x_offset, current_y_offset, max_line_height, max_x_offset,
                                 current_line, has_prev_block, pending_prev_block_bottom_margin, has_any_flow_content);

            cleanup_line(has_prev_block, pending_prev_block_bottom_margin, current_line,
                         current_y_offset, current_x_offset, max_line_height, max_x_offset);

            resolve_height(container_dimensions, current_y_offset, max_line_height, max_x_offset, current_x_offset);
        }

    }

} // namespace linweb
