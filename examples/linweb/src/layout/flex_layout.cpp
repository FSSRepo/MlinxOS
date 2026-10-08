#include "layout_engine.h"
#include <vector>
#include <memory>
#include <string>
#include <sstream>
#include <algorithm>

namespace linweb
{

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
        float total_flex_grow = 0;
        std::vector<float> flex_grow_values;
        flex_grow_values.reserve(children.size());

        for (const auto &child : children)
        {
            std::string grow_str = child->styled_node->value("flex-grow");
            float grow = 0;
            if (!grow_str.empty())
            {
                try { grow = std::stof(grow_str); }
                catch (...) { grow = 0; }
            }
            flex_grow_values.push_back(grow);
            total_flex_grow += grow;
        }

        if (total_flex_grow <= 0.001f)
            return;

        float container_main_size = is_column ? dimensions.content.height : dimensions.content.width;

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

        float total_gaps = (children.size() > 1) ? (children.size() - 1) * specified_gap : 0;
        float free_space = container_main_size - main_offset - total_gaps;

        if (free_space <= 0.001f)
            return;

        for (size_t i = 0; i < children.size(); ++i)
        {
            if (flex_grow_values[i] <= 0)
                continue;

            float extra = free_space * (flex_grow_values[i] / total_flex_grow);

            auto &child = children[i];

            float new_main = (is_column ? child->dimensions.content.height : child->dimensions.content.width) + extra;

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
            else if (justify_content == "flex-end")
                start_main_offset = free_space;
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
                        stretch_container.content.width = available_cross;
                        stretch_container.content.height = child->dimensions.content.height;
                        child->layout(stretch_container);
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
                        stretch_container.content.width = child->dimensions.content.width;
                        stretch_container.content.height = available_cross;
                        child->layout(stretch_container);
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
            child_container.content.width = child->dimensions.content.width;
            child_container.content.height = child->dimensions.content.height;
            child->layout(child_container);

            current_main += child_main + main_gap;
        }
    }

    /**
     * @brief Performs the Flexbox layout.
     *
     * @param container_dimensions Container dimensions.
     * @param normal_flow_children Children in the normal flow.
     */
    void LayoutBox::layout_flex(Dimensions container_dimensions, const std::vector<std::shared_ptr<LayoutBox>> &normal_flow_children)
    {
        std::string flex_dir = styled_node->value("flex-direction");
        bool is_column = (flex_dir == "column");

        float main_offset = 0;
        float cross_size = 0;
        flex_initial_pass(container_dimensions, normal_flow_children, is_column, main_offset, cross_size);

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

    std::string w_str = styled_node->value("width");
    if (w_str.empty()) {
        float content_main = total_main_size +
                             dimensions.padding.left + dimensions.padding.right +
                             dimensions.border.left + dimensions.border.right;
        if (content_main > dimensions.content.width) {
            dimensions.content.width = content_main;
        }
    }

    float current_y_offset = is_column ? total_main_size : cross_size;
        float max_x_offset = is_column ? cross_size : total_main_size;
        resolve_height(container_dimensions, current_y_offset, 0, max_x_offset, 0);

        float start_main_offset = 0;
        float main_gap = 0;
        flex_justify_content(normal_flow_children, is_column, main_offset, start_main_offset, main_gap);

        flex_align_and_position(normal_flow_children, is_column, start_main_offset, main_gap);
    }

} // namespace linweb
