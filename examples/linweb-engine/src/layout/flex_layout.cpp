#include "layout_engine.h"

#include <vector>
#include <memory>
#include <string>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace linweb
{

    // The flex factors of an item. An empty basis is `auto` (the item's own size).
    struct FlexFactors
    {
        float grow = 0;
        float shrink = 1;
        std::string basis;
    };

    static float number_or(const std::string &s, float fallback)
    {
        try { return std::stof(s); }
        catch (...) { return fallback; }
    }

    static bool is_plain_number(const std::string &s)
    {
        return !s.empty() && s.find_first_not_of("0123456789.") == std::string::npos;
    }

    // From the flex shorthand (flex: <grow> <shrink>? <basis>?; a single length or % is the basis) and then the
    // longhands, which win over the shorthand.
    static FlexFactors flex_factors_of(const std::shared_ptr<StyledNode> &styled)
    {
        FlexFactors f;
        std::stringstream ss(styled->value("flex"));
        std::vector<std::string> parts;
        for (std::string p; ss >> p;)
            parts.push_back(p);

        if (parts.size() == 1 && parts[0] == "none") { f.grow = 0; f.shrink = 0; }
        else if (parts.size() == 1 && parts[0] == "auto") { f.grow = 1; f.shrink = 1; }
        else if (parts.size() == 1 && is_plain_number(parts[0])) { f.grow = number_or(parts[0], 0); f.basis = "0px"; }
        else if (parts.size() == 1) { f.grow = 1; f.basis = parts[0]; }
        else if (parts.size() >= 2)
        {
            f.grow = number_or(parts[0], 0);
            if (is_plain_number(parts[1]))
            {
                f.shrink = number_or(parts[1], 1);
                if (parts.size() > 2) f.basis = parts[2];
            }
            else
                f.basis = parts[1];
        }

        std::string grow = styled->value("flex-grow");
        if (!grow.empty()) f.grow = number_or(grow, f.grow);
        std::string shrink = styled->value("flex-shrink");
        if (!shrink.empty()) f.shrink = number_or(shrink, f.shrink);
        std::string basis = styled->value("flex-basis");
        if (!basis.empty()) f.basis = basis;
        if (f.basis == "auto") f.basis.clear();   // whether it came from the shorthand or the longhand
        return f;
    }

    static float compute_outer_main(const std::shared_ptr<LayoutBox>& child, bool is_column)
    {
        if (is_column)
            return child->dimensions.content.height +
                   child->dimensions.padding.top + child->dimensions.padding.bottom +
                   child->dimensions.border.top + child->dimensions.border.bottom +
                   child->dimensions.margin.top + child->dimensions.margin.bottom;
        else
            return child->dimensions.content.width +
                   child->dimensions.padding.left + child->dimensions.padding.right +
                   child->dimensions.border.left + child->dimensions.border.right +
                   child->dimensions.margin.left + child->dimensions.margin.right;
    }

    static float compute_outer_cross(const std::shared_ptr<LayoutBox>& child, bool is_column)
    {
        if (is_column)
            return child->dimensions.content.width +
                   child->dimensions.padding.left + child->dimensions.padding.right +
                   child->dimensions.border.left + child->dimensions.border.right +
                   child->dimensions.margin.left + child->dimensions.margin.right;
        else
            return child->dimensions.content.height +
                   child->dimensions.padding.top + child->dimensions.padding.bottom +
                   child->dimensions.border.top + child->dimensions.border.bottom +
                   child->dimensions.margin.top + child->dimensions.margin.bottom;
    }

    /**
     * @brief Performs an initial design pass for Flexbox to determine the elements' size.
     *
     * @param children Children to process.
     * @param is_column True if the direction is column.
     * @param main_offset Reference to accumulate the size on the main axis.
     * @param cross_size Reference to store the maximum size on the cross axis.
     */
    void LayoutBox::flex_initial_pass(Dimensions container_dimensions, const std::vector<std::shared_ptr<LayoutBox>> &children, bool is_column, float &main_offset, float &cross_size)
    {
        float container_content_width = dimensions.content.width;
        float container_content_height = dimensions.content.height;

        if (container_content_width <= 0.001f)
            container_content_width = container_dimensions.content.width;
        if (container_content_height <= 0.001f)
            container_content_height = container_dimensions.content.height;

        for (const auto &child : children)
        {
            Dimensions child_container;
            child_container.content.x = dimensions.content.x;
            child_container.content.y = dimensions.content.y;

            if (is_column)
            {
                child_container.content.width = container_content_width;
                child_container.content.height = container_content_height;
            }
            else
            {
                child_container.content.width = container_content_width;
                child_container.content.height = container_content_height;
            }
            child->layout(child_container);

            float child_main = compute_outer_main(child, is_column);
            float child_cross = compute_outer_cross(child, is_column);

            main_offset += child_main;
            cross_size = std::max(cross_size, child_cross);
        }
    }

    /**
     * @brief Distributes the extra space among the children according to the flex-grow property.
     *
     * @param children Flexbox children.
     * @param is_column True if the direction is column.
     * @param main_offset Total size of the children on the main axis (updated).
     */
    void LayoutBox::flex_distribute_grow(Dimensions container_dimensions, const std::vector<std::shared_ptr<LayoutBox>> &children, bool is_column, float &main_offset)
    {
        // A flex container with an auto width inside a row is as wide as its items' hypothetical sizes (max-content),
        // so there is no free space to share out and no item is shrunk; its width is worked out from the items.
        auto parent_flex = parent.lock();
        // only with a content-based basis: an item with a basis of its own (flex: 1 means 0px) takes the size it is given
        bool sized_by_content = parent_flex && parent_flex->box_type == BoxType::Flex &&
                                parent_flex->styled_node->value("flex-direction") != "column" &&
                                styled_node->value("width").empty() &&
                                flex_factors_of(styled_node).basis.empty();
        if (!is_column && sized_by_content)
            return;
        // a column with an auto height has no free space: it is as tall as its items, so none is shrunk or grown
        if (is_column && (styled_node->value("height").empty() || styled_node->value("height") == "auto"))
            return;

        float container_main_size = is_column ? dimensions.content.height : dimensions.content.width;

        // each item's flex-grow, flex-shrink and flex-basis (from the longhands or the flex shorthand)
        std::vector<FlexFactors> factors;
        factors.reserve(children.size());
        for (const auto &child : children)
            factors.push_back(flex_factors_of(child->styled_node));

        std::string gap_str = styled_node->value("gap");
        if (gap_str.empty())
        {
            gap_str = is_column ? styled_node->value("row-gap") : styled_node->value("column-gap");
        }
        float specified_gap = 0;
        if (!gap_str.empty())
        {
            specified_gap = LayoutEngine::parse_px(gap_str, container_main_size);
        }

        // the hypothetical size of each item: its flex-basis when it has one, otherwise the size it was laid out at
        std::vector<float> base(children.size()), target(children.size());
        float outer_sum = 0;
        for (size_t i = 0; i < children.size(); ++i)
        {
            const auto &child = children[i];
            float current = is_column ? child->dimensions.content.height : child->dimensions.content.width;
            float outer_now = compute_outer_main(child, is_column);
            base[i] = factors[i].basis.empty() ? current : LayoutEngine::parse_px(factors[i].basis, container_main_size);
            outer_sum += base[i] + (outer_now - current);
        }

        float total_gaps = (children.size() > 1) ? (children.size() - 1) * specified_gap : 0;
        float free_space = container_main_size - outer_sum - total_gaps;

        float total_grow = 0, total_shrink = 0;
        for (size_t i = 0; i < children.size(); ++i)
        {
            total_grow += factors[i].grow;
            total_shrink += factors[i].shrink * base[i];
        }
        for (size_t i = 0; i < children.size(); ++i)
        {
            target[i] = base[i];
            if (free_space > 0.001f && total_grow > 0.001f)
                target[i] = base[i] + free_space * (factors[i].grow / total_grow);
            else if (free_space < -0.001f && total_shrink > 0.001f)
                target[i] = std::max(0.0f, base[i] + free_space * (factors[i].shrink * base[i] / total_shrink));
        }

        for (size_t i = 0; i < children.size(); ++i)
        {
            float current = is_column ? children[i]->dimensions.content.height : children[i]->dimensions.content.width;
            if (std::abs(target[i] - current) < 0.01f)
                continue;

            auto &child = children[i];

            float new_main = target[i];

            Dimensions grow_container;
            grow_container.content.x = child->dimensions.content.x -
                                       child->dimensions.margin.left -
                                       child->dimensions.padding.left -
                                       child->dimensions.border.left;
            grow_container.content.y = child->dimensions.content.y -
                                       child->dimensions.margin.top -
                                       child->dimensions.padding.top -
                                       child->dimensions.border.top;

            if (is_column)
            {
                float height_with_padding_border = new_main +
                                                   child->dimensions.padding.top +
                                                   child->dimensions.padding.bottom +
                                                   child->dimensions.border.top +
                                                   child->dimensions.border.bottom;
                grow_container.content.width = dimensions.content.width;
                grow_container.content.height = height_with_padding_border;
                child->layout(grow_container);
                child->dimensions.content.height = new_main;
            }
            else
            {
                float width_with_padding_border = new_main +
                                                  child->dimensions.padding.left +
                                                  child->dimensions.padding.right +
                                                  child->dimensions.border.left +
                                                  child->dimensions.border.right;
                grow_container.content.width = width_with_padding_border;
                grow_container.content.height = dimensions.content.height > 0.001f ? dimensions.content.height : container_dimensions.content.height;
                child->layout(grow_container);
                child->dimensions.content.width = new_main;
            }

            Dimensions child_relayout;
            child_relayout.content.x = child->dimensions.content.x -
                                       child->dimensions.margin.left -
                                       child->dimensions.padding.left -
                                       child->dimensions.border.left;
            child_relayout.content.y = child->dimensions.content.y -
                                       child->dimensions.margin.top -
                                       child->dimensions.padding.top -
                                       child->dimensions.border.top;
            child_relayout.content.width = child->dimensions.content.width;
            child_relayout.content.height = child->dimensions.content.height;
            child->layout_children(child_relayout);
            // the size the main axis gave the item is its size; laying out its children may have resolved it again
            if (is_column) child->dimensions.content.height = new_main;
        }

        main_offset = 0;
        for (const auto &child : children)
        {
            main_offset += compute_outer_main(child, is_column);
        }
    }

    /**
     * @brief Calculates the initial offset and the space between elements based on justify-content.
     *
     * @param children Flexbox children.
     * @param is_column True if the direction is column.
     * @param main_offset Total size of the children on the main axis.
     * @param start_main_offset Reference to store the starting offset.
     * @param main_gap Reference to store the space between elements.
     */
    void LayoutBox::flex_justify_content(const std::vector<std::shared_ptr<LayoutBox>> &children, bool is_column, float main_offset, float &start_main_offset, float &main_gap)
    {
        std::string gap_str = styled_node->value("gap");
        if (gap_str.empty())
        {
            if (is_column)
                gap_str = styled_node->value("row-gap");
            else
                gap_str = styled_node->value("column-gap");
        }

        float container_main_size = is_column ? dimensions.content.height : dimensions.content.width;
        float specified_gap = 0;
        if (!gap_str.empty())
        {
            specified_gap = LayoutEngine::parse_px(gap_str, container_main_size);
        }

        std::string justify_content = styled_node->value("justify-content");
        if (justify_content.empty())
            justify_content = "flex-start";

        float total_main_with_gaps = main_offset + (children.size() > 1 ? (children.size() - 1) * specified_gap : 0);
        float free_space = container_main_size - total_main_with_gaps;

        main_gap = specified_gap;

        if (free_space > 0)
        {
            if (justify_content == "center")
                start_main_offset = free_space / 2.0f;
            else if (justify_content == "flex-end" || justify_content == "end")
                start_main_offset = free_space;
            else if (justify_content == "space-evenly")
            {
                // equal spaces between the items and at both ends
                float extra_gap = free_space / (children.size() + 1);
                main_gap = specified_gap + extra_gap;
                start_main_offset = extra_gap;
            }
            else if (justify_content == "space-between" && children.size() > 1)
                main_gap = specified_gap + (free_space / (children.size() - 1));
            else if (justify_content == "space-around")
            {
                float extra_gap = free_space / children.size();
                main_gap = specified_gap + extra_gap;
                start_main_offset = extra_gap / 2.0f;
            }
        }
    }

    /**
     * @brief Aligns and positions Flexbox elements on the cross axis.
     *
     * @param children Flexbox children.
     * @param is_column True if the direction is column.
     * @param start_main_offset Initial offset on the main axis.
     * @param main_gap Space between elements.
     */
    void LayoutBox::flex_align_and_position(const std::vector<std::shared_ptr<LayoutBox>> &children, bool is_column, float start_main_offset, float main_gap)
    {
        std::string align_items = styled_node->value("align-items");
        if (align_items.empty())
            align_items = "stretch";

        float current_main = start_main_offset;
        for (const auto &child : children)
        {
            float child_main = compute_outer_main(child, is_column);
            float child_cross = compute_outer_cross(child, is_column);

            std::string child_align = child->styled_node->value("align-self");
            if (child_align.empty() || child_align == "auto")
                child_align = align_items;

            float current_cross = 0;
            float container_cross_size = is_column ? dimensions.content.width : dimensions.content.height;

            if (child_align == "center")
                current_cross = (container_cross_size - child_cross) / 2.0f;
            else if (child_align == "flex-end")
                current_cross = container_cross_size - child_cross;
            else if (child_align == "stretch")
            {
                if (is_column)
                {
                    float available_cross = container_cross_size -
                                            child->dimensions.padding.left - child->dimensions.padding.right -
                                            child->dimensions.border.left - child->dimensions.border.right -
                                            ((child->dimensions.margin.left == -12345.0f) ? 0 : child->dimensions.margin.left) -
                                            ((child->dimensions.margin.right == -12345.0f) ? 0 : child->dimensions.margin.right);
                    if (child->dimensions.content.width < available_cross - 0.001f)
                    {
                        Dimensions stretch_container;
                        stretch_container.content.x = child->dimensions.content.x -
                                                      child->dimensions.margin.left -
                                                      child->dimensions.padding.left -
                                                      child->dimensions.border.left;
                        stretch_container.content.y = child->dimensions.content.y -
                                                      child->dimensions.margin.top -
                                                      child->dimensions.padding.top -
                                                      child->dimensions.border.top;
                        // the child takes its own padding, border and margins off this width, so pass the whole cross size
                        float main_height = child->dimensions.content.height;
                        stretch_container.content.width = container_cross_size;
                        stretch_container.content.height = main_height;
                        child->layout(stretch_container);
                        child->dimensions.content.height = main_height;   // the height came from the distribution
                    }
                }
                else
                {
                    float available_cross = container_cross_size -
                                            child->dimensions.padding.top - child->dimensions.padding.bottom -
                                            child->dimensions.border.top - child->dimensions.border.bottom -
                                            ((child->dimensions.margin.top == -12345.0f) ? 0 : child->dimensions.margin.top) -
                                            ((child->dimensions.margin.bottom == -12345.0f) ? 0 : child->dimensions.margin.bottom);
                    if (child->dimensions.content.height < available_cross - 0.001f)
                    {
                        Dimensions stretch_container;
                        stretch_container.content.x = child->dimensions.content.x -
                                                      child->dimensions.margin.left -
                                                      child->dimensions.padding.left -
                                                      child->dimensions.border.left;
                        stretch_container.content.y = child->dimensions.content.y -
                                                      child->dimensions.margin.top -
                                                      child->dimensions.padding.top -
                                                      child->dimensions.border.top;
                        // the width is the one the main axis gave the item: laying it out again would shrink it to its content
                        float main_width = child->dimensions.content.width;
                        stretch_container.content.width = main_width;
                        stretch_container.content.height = available_cross;
                        child->layout(stretch_container);
                        child->dimensions.content.width = main_width;
                    }
                }
            }

            if (is_column)
            {
                child->dimensions.content.x = dimensions.content.x + current_cross +
                                              ((child->dimensions.margin.left == -12345.0f) ? 0 : child->dimensions.margin.left) +
                                              child->dimensions.padding.left + child->dimensions.border.left;
                child->dimensions.content.y = dimensions.content.y + current_main +
                                              ((child->dimensions.margin.top == -12345.0f) ? 0 : child->dimensions.margin.top) +
                                              child->dimensions.padding.top + child->dimensions.border.top;
            }
            else
            {
                child->dimensions.content.x = dimensions.content.x + current_main +
                                              ((child->dimensions.margin.left == -12345.0f) ? 0 : child->dimensions.margin.left) +
                                              child->dimensions.padding.left + child->dimensions.border.left;
                child->dimensions.content.y = dimensions.content.y + current_cross +
                                              ((child->dimensions.margin.top == -12345.0f) ? 0 : child->dimensions.margin.top) +
                                              child->dimensions.padding.top + child->dimensions.border.top;
            }

            Dimensions child_container;
            child_container.content.x = child->dimensions.content.x -
                                        ((child->dimensions.margin.left == -12345.0f) ? 0 : child->dimensions.margin.left) -
                                        child->dimensions.padding.left - child->dimensions.border.left;
            child_container.content.y = child->dimensions.content.y -
                                        ((child->dimensions.margin.top == -12345.0f) ? 0 : child->dimensions.margin.top) -
                                        child->dimensions.padding.top - child->dimensions.border.top;
            // a child in a column takes its width from the container (it removes its own padding and margins from it)
            child_container.content.height = child->dimensions.content.height;
            // a percentage or calc() height inside the item refers to the flex container's height, not the item's own
            if (is_column && dimensions.content.height < 999990.0f)
                child_container.content.height = dimensions.content.height;
            // only an item the free space was shared out to has a size of its own to keep; a content-sized one keeps what its layout worked out
            FlexFactors item = flex_factors_of(child->styled_node);
            bool distributed = !item.basis.empty() || item.grow > 0;
            if (is_column)
            {
                // a child in a column takes its width from the container (it removes its own padding and margins from it)
                // the height was set when the main axis was distributed; laying the item out again would reset it to its content
                float main_height = child->dimensions.content.height;
                child_container.content.width = container_cross_size;
                child->layout(child_container);
                child->dimensions.content.height = main_height;
                child->layout_children(child_container);
                child->center_button_content();
                if (distributed) child->dimensions.content.height = main_height;   // laying out its children may resolve it again
            }
            else
            {
                // in a row the width was set when the main axis was distributed; laying it out again would shrink it to its content
                float main_width = child->dimensions.content.width;
                child_container.content.width = main_width;
                child->layout_children(child_container);
                child->center_button_content();
                if (distributed) child->dimensions.content.width = main_width;
            }

            current_main += child_main + main_gap;
        }
    }

    /**
     * @brief Performs the Flexbox layout.
     *
     * @param container_dimensions Container dimensions.
     * @param normal_flow_children Children in the normal flow.
     */
    // flex-wrap: wrap / wrap-reverse. The items are measured against the line length (the width of a row, the height
    // of a column) and split into lines where they run out of room. Each line is laid out as a flex line of its own;
    // the lines are then placed along the cross axis by align-content, with row-gap (or gap) between them.
    // wrap-reverse stacks the lines the other way round. A column only wraps when its height is definite.
    void LayoutBox::layout_flex(Dimensions container_dimensions, const std::vector<std::shared_ptr<LayoutBox>> &normal_flow_children)
    {
        std::string wrap = styled_node->value("flex-wrap");
        bool is_column = styled_node->value("flex-direction") == "column";
        if ((wrap != "wrap" && wrap != "wrap-reverse") || normal_flow_children.empty())
        {
            layout_flex_items(container_dimensions, normal_flow_children);
            return;
        }
        bool reverse = wrap == "wrap-reverse";

        std::string own_height = styled_node->value("height");
        float explicit_height = own_height.empty() ? 0.0f : LayoutEngine::parse_px(own_height, container_dimensions.content.height);
        float line_length = is_column ? explicit_height : dimensions.content.width;
        if (line_length <= 0.001f)
        {
            layout_flex_items(container_dimensions, normal_flow_children);
            return;
        }

        std::string gap_str = styled_node->value("gap");
        std::string column_gap_str = styled_node->value("column-gap");
        if (column_gap_str.empty()) column_gap_str = gap_str;
        std::string row_gap_str = styled_node->value("row-gap");
        if (row_gap_str.empty()) row_gap_str = gap_str;
        float column_gap = LayoutEngine::parse_px(column_gap_str, dimensions.content.width);
        float row_gap = LayoutEngine::parse_px(row_gap_str, container_dimensions.content.height);
        float main_gap = is_column ? row_gap : column_gap;     // between the items of a line
        float cross_gap = is_column ? column_gap : row_gap;    // between the lines

        // break the items into lines
        std::vector<std::vector<std::shared_ptr<LayoutBox>>> lines;
        float used = 0;
        for (const auto &child : normal_flow_children)
        {
            Dimensions probe;
            probe.content.x = dimensions.content.x;
            probe.content.y = dimensions.content.y;
            probe.content.width = is_column ? dimensions.content.width : dimensions.content.width;
            probe.content.height = is_column ? line_length : container_dimensions.content.height;
            child->layout(probe);
            float outer = compute_outer_main(child, is_column);

            if (lines.empty() || (!lines.back().empty() && used + main_gap + outer > line_length + 0.001f))
            {
                lines.emplace_back();
                used = 0;
            }
            if (!lines.back().empty()) used += main_gap;
            lines.back().push_back(child);
            used += outer;
        }
        if (lines.size() <= 1)
        {
            layout_flex_items(container_dimensions, normal_flow_children);
            return;
        }

        // size of each line across: lay the lines out once at the origin to measure them
        float origin = is_column ? dimensions.content.x : dimensions.content.y;
        std::vector<float> sizes;
        for (const auto &line : lines)
        {
            if (is_column) dimensions.content.x = origin;
            else dimensions.content.y = origin;
            layout_flex_items(container_dimensions, line);
            float size = 0;
            for (const auto &child : line)
                size = std::max(size, compute_outer_cross(child, is_column));
            sizes.push_back(size);
        }

        // align-content: the free space across the container is shared out between the lines
        std::string align = styled_node->value("align-content");
        std::vector<float> measured = sizes;   // each line before the align-content stretch
        float total = 0;
        for (float s : sizes) total += s;
        float cross_total = total + cross_gap * static_cast<float>(sizes.size() - 1);
        // the space across is the container's own definite size: its width for a column, its height for a row
        float explicit_width = styled_node->value("width").empty() ? 0.0f : LayoutEngine::parse_px(styled_node->value("width"), container_dimensions.content.width);
        float available = is_column ? explicit_width : explicit_height;
        if (available <= 0.001f) available = cross_total;   // no definite size across: nothing to share out
        float free = std::max(0.0f, available - cross_total);
        float start = 0, between = 0;
        size_t n = sizes.size();
        if (align == "center") start = free / 2;
        else if (align == "flex-end" || align == "end") start = free;
        else if (align == "space-between") between = n > 1 ? free / static_cast<float>(n - 1) : 0;
        else if (align == "space-around") { between = free / static_cast<float>(n); start = between / 2; }
        else if (align == "space-evenly") { between = free / static_cast<float>(n + 1); start = between; }
        else if (align.empty() || align == "normal" || align == "stretch")
        {
            for (float &s : sizes) s += free / static_cast<float>(n);
        }

        // place the lines (wrap-reverse starts from the other end) and lay each one out at its place
        std::vector<size_t> order;
        for (size_t i = 0; i < n; ++i) order.push_back(i);
        if (reverse) std::reverse(order.begin(), order.end());
        float position = start;
        std::vector<float> line_at(n, 0.0f);
        for (size_t k = 0; k < n; ++k)
        {
            line_at[order[k]] = position;
            position += sizes[order[k]] + between + cross_gap;
        }
        for (size_t i = 0; i < n; ++i)
        {
            if (is_column) dimensions.content.x = origin + line_at[i];
            else dimensions.content.y = origin + line_at[i];
            // a column line is as wide as its widest item: its items stretch to that, not to the container
            float saved_width = dimensions.content.width;
            if (is_column) dimensions.content.width = measured[i];
            layout_flex_items(container_dimensions, lines[i]);
            dimensions.content.width = saved_width;
        }
        if (is_column) dimensions.content.x = origin;
        else dimensions.content.y = origin;

        if (is_column)
        {
            if (styled_node->value("width").empty()) dimensions.content.width = cross_total;
        }
        else if (styled_node->value("height").empty())
        {
            dimensions.content.height = cross_total;
        }
    }

    void LayoutBox::layout_flex_items(Dimensions container_dimensions, const std::vector<std::shared_ptr<LayoutBox>> &normal_flow_children)
    {
        std::string flex_dir = styled_node->value("flex-direction");
        bool is_column = (flex_dir == "column");

        float main_offset = 0;
        float cross_size = 0;
        flex_initial_pass(container_dimensions, normal_flow_children, is_column, main_offset, cross_size);

        // a column's free space is its height, so an explicit height has to be known before it is shared out
        std::string own_height = styled_node->value("height");
        if (is_column && !own_height.empty()) {
            float h = own_height.find('%') != std::string::npos
                          ? LayoutEngine::parse_px(own_height, container_dimensions.content.height)
                          : LayoutEngine::parse_px(own_height, LayoutEngine::parse_px(styled_node->value("font-size"), 16.0f));
            if (styled_node->value("box-sizing") == "border-box")
                h -= dimensions.padding.top + dimensions.padding.bottom + dimensions.border.top + dimensions.border.bottom;
            dimensions.content.height = std::max(0.0f, h);
        }

        flex_distribute_grow(container_dimensions, normal_flow_children, is_column, main_offset);

        std::string gap_str = styled_node->value("gap");
        if (gap_str.empty())
        {
            gap_str = is_column ? styled_node->value("row-gap") : styled_node->value("column-gap");
        }
        float specified_gap = 0;
        if (!gap_str.empty())
        {
            float container_main_size = is_column ? dimensions.content.height : dimensions.content.width;
            specified_gap = LayoutEngine::parse_px(gap_str, container_main_size);
        }

        float total_main_size = main_offset + (normal_flow_children.size() > 1 ? (normal_flow_children.size() - 1) * specified_gap : 0);

    // A flex container with an auto width that is an item of a row is as wide as its items (shrink-to-fit). A
    // block-level one fills the width it was given: its items never make it wider.
    auto parent_flex = parent.lock();
    bool in_row = parent_flex && parent_flex->box_type == BoxType::Flex && parent_flex->styled_node->value("flex-direction") != "column";
    // an item with its own flex-basis takes the width the row gave it, so only a content-based one is measured here
    if (in_row && styled_node->value("width").empty() && flex_factors_of(styled_node).basis.empty()) {
        // a column is as wide as its widest item; a row is as wide as its items laid end to end
        float content_main = (is_column ? cross_size : total_main_size) +
                             dimensions.padding.left + dimensions.padding.right +
                             dimensions.border.left + dimensions.border.right;
        // min-width still holds: it was applied when the width was resolved, and this measure must not undo it
        std::string min_w = styled_node->value("min-width");
        if (!min_w.empty() && min_w != "auto") {
            float minimum = LayoutEngine::parse_px(min_w, min_w.find('%') != std::string::npos ? container_dimensions.content.width : LayoutEngine::parse_px(styled_node->value("font-size"), 16.0f));
            if (styled_node->value("box-sizing") == "border-box")
                minimum -= dimensions.padding.left + dimensions.padding.right + dimensions.border.left + dimensions.border.right;
            content_main = std::max(content_main, minimum);
        }
        dimensions.content.width = content_main;
    }

    float current_y_offset = is_column ? total_main_size : cross_size;
        float max_x_offset = is_column ? cross_size : total_main_size;
        resolve_height(container_dimensions, current_y_offset, 0, max_x_offset, 0);

        float start_main_offset = 0;
        float main_gap = 0;
        flex_justify_content(normal_flow_children, is_column, main_offset, start_main_offset, main_gap);

        flex_align_and_position(normal_flow_children, is_column, start_main_offset, main_gap);

        // Items were moved after their own layout, so their absolutely positioned children must be placed again
        // from the final position of each item (their containing block).
        for (const auto &child : normal_flow_children)
            child->layout_positioned_children();
    }

} // namespace linweb
