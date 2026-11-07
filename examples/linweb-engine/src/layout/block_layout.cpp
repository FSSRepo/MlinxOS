#include "layout_engine.h"
#include <vector>
#include <memory>
#include <algorithm>
#include <cctype>
#include <functional>
#include "renderer/renderer.h"
#include "renderer/renderer_internal.h"
#include "renderer/custom_font.h"

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
     * @brief Centers a button's in-flow content vertically, as browsers do with a button taller than its content.
     *
     * A flex button aligns its items itself, so it is left alone (moving them again would count the offset twice).
     */
    void LayoutBox::center_button_content()
    {
        if (styled_node->node->tag_name != "button" || box_type == BoxType::Flex)
            return;

        float top = 0, bottom = 0;
        bool any = false;
        for (const auto &child : children)
        {
            if (child->position == PositionType::Absolute || child->position == PositionType::Fixed)
                continue;
            float child_top = child->dimensions.content.y - child->dimensions.margin.top - child->dimensions.padding.top - child->dimensions.border.top;
            float child_bottom = child->dimensions.content.y + child->dimensions.content.height + child->dimensions.padding.bottom +
                                 child->dimensions.border.bottom + child->dimensions.margin.bottom;
            top = any ? std::min(top, child_top) : child_top;
            bottom = any ? std::max(bottom, child_bottom) : child_bottom;
            any = true;
        }
        if (!any)
            return;

        float free = dimensions.content.height - (bottom - top);
        if (free <= 0.001f)
            return;
        float offset = free / 2.0f;

        for (const auto &child : children)
        {
            if (child->position == PositionType::Absolute || child->position == PositionType::Fixed)
                continue;
            Dimensions shifted;
            shifted.content.x = child->dimensions.content.x - child->dimensions.margin.left - child->dimensions.padding.left - child->dimensions.border.left;
            shifted.content.y = child->dimensions.content.y - child->dimensions.margin.top - child->dimensions.padding.top - child->dimensions.border.top + offset;
            shifted.content.width = child->dimensions.content.width;
            shifted.content.height = child->dimensions.content.height;
            child->layout(shifted);
        }
    }

    /**
     * @brief Applies text alignment (left, center, right) to a line of elements.
     *
     * @param line Vector of boxes that make up the line.
     * @param line_width Total width occupied by the elements of the line.
     */
    void LayoutBox::apply_line_alignment(const std::vector<std::shared_ptr<LayoutBox>> &line, float line_width)
    {
        if (line.empty()) return;
        // A box laid out again in place takes its own width as its container's, except when that width is a percentage:
        // that one refers to this block
        auto relayout_width = [this](const std::shared_ptr<LayoutBox>& box) {
            return box->styled_node->value("width").find('%') != std::string::npos ? dimensions.content.width : box->dimensions.content.width;
        };
        // An inline wrapper can establish its baseline through a descendant
        // with vertical-align: middle (for example an icon inside a link).
        auto middle_aligned = [](const std::shared_ptr<LayoutBox>& box) {
            auto current = box;
            while (current) {
                std::string align = current->styled_node->value("vertical-align");
                if (!align.empty() && align != "baseline") return align == "middle";
                if (current->box_type != BoxType::Inline || current->children.size() != 1) break;
                current = current->children.front();
            }
            return false;
        };
        float line_height = 0;
        for (const auto& box : line) {
            line_height = std::max(line_height, box->dimensions.content.height + box->dimensions.padding.top +
                box->dimensions.padding.bottom + box->dimensions.border.top + box->dimensions.border.bottom +
                box->dimensions.margin.top + box->dimensions.margin.bottom);
        }
        for (const auto& box : line) {
            if (!middle_aligned(box)) continue;
            float height = box->dimensions.content.height + box->dimensions.padding.top + box->dimensions.padding.bottom +
                box->dimensions.border.top + box->dimensions.border.bottom + box->dimensions.margin.top + box->dimensions.margin.bottom;
            float offset = (line_height - height) / 2.0f;
            if (offset <= 0) continue;
            Dimensions shifted;
            shifted.content.x = box->dimensions.content.x - box->dimensions.margin.left - box->dimensions.padding.left - box->dimensions.border.left;
            shifted.content.y = box->dimensions.content.y - box->dimensions.margin.top - box->dimensions.padding.top - box->dimensions.border.top + offset;
            shifted.content.width = relayout_width(box);
            shifted.content.height = box->dimensions.content.height;
            box->layout(shifted);
        }
        // Everything in a line sits on one baseline: text of another size, an image (its bottom edge) or an inline-block
        // (the baseline of its last line) is moved down to it. Positions are compared as they are, so aligning again
        // after the boxes were moved changes nothing.
        {
            auto side = [](float v) { return v == -12345.0f ? 0.0f : v; };
            // the absolute y of the baseline of a text box
            auto text_baseline = [](const std::shared_ptr<LayoutBox>& text) {
                float font_size = LayoutEngine::parse_px(text->styled_node->value("font-size"), 16.0f);
                if (font_size <= 0) font_size = 16.0f;
                const CustomFont* custom = CustomFonts::select(text->styled_node->value("font-family"), text->styled_node->value("font-weight"), text->styled_node->value("font-style"));
                const float scale = font_size / RendererState::base_font_size;
                const float font_height = Renderer::get_line_height(font_size, custom);
                const float row = text->dimensions.content.height / static_cast<float>(std::max(1, text->text_line_count));
                // a wrapped text sits on the baseline of its first line when it starts the line, of its last otherwise
                const float rows_above = text->text_line_count > 1 ? static_cast<float>(text->text_line_count - 1) * row : 0.0f;
                return text->dimensions.content.y + rows_above + std::max(0.0f, row - font_height) / 2.0f + Renderer::scaled_ascent(font_size, custom);
            };
            auto is_replaced = [](const LayoutBox& box) {
                const std::string& tag = box.styled_node->node->tag_name;
                return tag == "img" || tag == "svg" || tag == "video" || tag == "canvas";
            };
            auto is_control = [](const LayoutBox& box) {
                const std::string& tag = box.styled_node->node->tag_name;
                return tag == "input" || tag == "select" || tag == "textarea" || tag == "button" || tag == "progress";
            };
            // the baseline of a box found from its content: the first text of an inline, the last line of an inline-block
            std::function<bool(const std::shared_ptr<LayoutBox>&, bool, float&)> baseline_of =
                [&](const std::shared_ptr<LayoutBox>& box, bool last, float& out) -> bool {
                if (box->styled_node->node->type() == NodeType::Text) {
                    // a text that goes on over several lines has a baseline for each; which one belongs to this line is not known here
                    if (is_whitespace_node(box) || box->text_line_count > 1) return false;
                    out = text_baseline(box);
                    return true;
                }
                if (is_control(*box)) return false;
                if (is_replaced(*box)) {
                    out = box->dimensions.content.y + box->dimensions.content.height + box->dimensions.padding.bottom +
                          box->dimensions.border.bottom + side(box->dimensions.margin.bottom);
                    return true;
                }
                if (last) {
                    for (auto it = box->children.rbegin(); it != box->children.rend(); ++it)
                        if (baseline_of(*it, true, out)) return true;
                } else {
                    for (const auto& child : box->children)
                        if (baseline_of(child, false, out)) return true;
                }
                return false;
            };
            std::vector<std::pair<std::shared_ptr<LayoutBox>, float>> baselines;
            float lowest = -1e9f;
            for (const auto& box : line) {
                if (is_whitespace_node(box) || middle_aligned(box) || box->is_empty_inline()) continue;
                const bool text = box->styled_node->node->type() == NodeType::Text;
                if (!text && box->box_type != BoxType::Inline && box->box_type != BoxType::InlineBlock) continue;
                float baseline = 0;
                if (!baseline_of(box, box->box_type == BoxType::InlineBlock, baseline)) {
                    if (box->box_type != BoxType::InlineBlock || is_control(*box)) continue;
                    baseline = box->dimensions.content.y + box->dimensions.content.height + box->dimensions.padding.bottom +
                               box->dimensions.border.bottom + side(box->dimensions.margin.bottom);
                }
                baselines.push_back({box, baseline});
                lowest = std::max(lowest, baseline);
            }
            for (const auto& entry : baselines) {
                const float shift = lowest - entry.second;
                if (shift < 0.25f) continue;
                const auto& box = entry.first;
                Dimensions shifted;
                shifted.content.x = box->dimensions.content.x - side(box->dimensions.margin.left) - box->dimensions.padding.left - box->dimensions.border.left;
                shifted.content.y = box->dimensions.content.y - side(box->dimensions.margin.top) - box->dimensions.padding.top - box->dimensions.border.top + shift;
                shifted.content.width = relayout_width(box);
                shifted.content.height = box->dimensions.content.height;
                box->layout(shifted);
            }
        }

        std::string text_align = styled_node->value("text-align");
        if (text_align == "left" || text_align.empty())
            return;

        // A collapsible space at the end of a line is removed before alignment, so it must not
        // count toward the width that gets centered or right-aligned.
        float trailing_space = 0;
        for (auto it = line.rbegin(); it != line.rend() && is_whitespace_node(*it); ++it)
            trailing_space += (*it)->dimensions.content.width;
        float extra_space = dimensions.content.width - (line_width - trailing_space);
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
                box_container.content.width = relayout_width(box);
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

        // only a line before the first block keeps the margin of that block apart from the parent's
        bool parent_has_inline_content = false;
        for (const auto &c : normal_flow_children)
        {
            if (is_whitespace_node(c)) continue;
            parent_has_inline_content = c->box_type == BoxType::Inline || c->box_type == BoxType::InlineBlock || c->styled_node->node->type() == NodeType::Text;
            break;
        }

        std::string parent_height_prop = styled_node->value("height");
        std::string parent_min_height_prop = styled_node->value("min-height");
        bool parent_has_specified_height = !parent_height_prop.empty() || !parent_min_height_prop.empty();

        bool is_root = (styled_node->node->tag_name == "html");
        std::string overflow = styled_node->value("overflow");
        bool is_scrollable = (overflow == "auto" || overflow == "scroll" || overflow == "hidden");

        // An inline-block, or a flex or grid item, starts its own formatting context: its children's margins stay
        // inside it, so they never collapse through its edges (they make it taller, or move its content, as in a browser).
        auto container = parent.lock();
        bool is_grid_or_flex_item = container && (container->box_type == BoxType::Flex || container->box_type == BoxType::Grid);
        bool is_block_context = box_type == BoxType::InlineBlock || is_grid_or_flex_item;
        bool parent_can_collapse_top = !is_root && !is_scrollable && !is_block_context && (dimensions.border.top == 0.0f && dimensions.padding.top == 0.0f && !parent_has_inline_content);
        bool parent_can_collapse_bottom = !is_root && !is_scrollable && !is_block_context && (dimensions.border.bottom == 0.0f && dimensions.padding.bottom == 0.0f && !parent_has_specified_height);

        auto layout_children_pass = [&](float &cur_x, float &cur_y, float &max_lh, float &max_x_off,
                                         std::vector<std::shared_ptr<LayoutBox>> &line,
                                         bool &has_prev, float &pending_margin, bool &has_flow)
        {
            for (const auto &child : normal_flow_children)
            {
                bool is_ws = is_whitespace_node(child);
                if (is_ws && line.empty()) continue;
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
        if (w_str.empty() && (box_type == BoxType::Inline || box_type == BoxType::InlineBlock) && max_x_offset > orig_content_width + 0.001f)
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
