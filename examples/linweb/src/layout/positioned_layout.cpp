#include "layout_engine.h"
#include "../renderer/renderer.h"
#include <vector>
#include <memory>
#include <string>

namespace linweb
{

    /**
     * @brief Applies the offset for elements with relative position.
     *
     * @param container_dimensions Container dimensions.
     */
    void LayoutBox::apply_relative_position(Dimensions container_dimensions)
    {
        if (position != PositionType::Relative)
            return;

        float parent_width = container_dimensions.content.width;
        float parent_height = container_dimensions.content.height;

        std::string top_str = styled_node->value("top");
        std::string bottom_str = styled_node->value("bottom");
        std::string left_str = styled_node->value("left");
        std::string right_str = styled_node->value("right");

        if (!top_str.empty())
            dimensions.content.y += LayoutEngine::parse_px(top_str, parent_height);
        else if (!bottom_str.empty())
            dimensions.content.y -= LayoutEngine::parse_px(bottom_str, parent_height);

        if (!left_str.empty())
            dimensions.content.x += LayoutEngine::parse_px(left_str, parent_width);
        else if (!right_str.empty())
            dimensions.content.x -= LayoutEngine::parse_px(right_str, parent_width);
    }

    void LayoutBox::layout_positioned_children()
    {
        float max_total_h = total_content_height;
        float max_total_w = dimensions.content.width + dimensions.padding.left + dimensions.padding.right + dimensions.border.left + dimensions.border.right;

        for (const auto &child : children)
        {
            if (child->position != PositionType::Absolute && child->position != PositionType::Fixed)
                continue;

            Dimensions child_container;
            float container_x, container_y, container_w, container_h;

            if (child->position == PositionType::Fixed)
            {
                container_x = 0;
                container_y = 0;
                container_w = (float)Renderer::get_viewport_width();
                container_h = (float)Renderer::get_viewport_height();
            }
            else if (position == PositionType::Static)
            {
                // Static elements should NOT be containing blocks for absolutely positioned children.
                // Walk up the parent chain to find the nearest positioned ancestor.
                // If none found, use the viewport (initial containing block).
                auto ancestor = parent.lock();
                bool found = false;
                while (ancestor)
                {
                    if (ancestor->position != PositionType::Static)
                    {
                        container_x = ancestor->dimensions.content.x - ancestor->dimensions.padding.left;
                        container_y = ancestor->dimensions.content.y - ancestor->dimensions.padding.top;
                        container_w = ancestor->dimensions.content.width + ancestor->dimensions.padding.left + ancestor->dimensions.padding.right;
                        container_h = ancestor->dimensions.content.height + ancestor->dimensions.padding.top + ancestor->dimensions.padding.bottom;
                        found = true;
                        break;
                    }
                    ancestor = ancestor->parent.lock();
                }
                if (!found)
                {
                    container_x = 0;
                    container_y = 0;
                    container_w = (float)Renderer::get_viewport_width();
                    container_h = (float)Renderer::get_viewport_height();
                }
            }
            else
            {
                container_x = dimensions.content.x - dimensions.padding.left;
                container_y = dimensions.content.y - dimensions.padding.top;
                container_w = dimensions.content.width + dimensions.padding.left + dimensions.padding.right;
                container_h = dimensions.content.height + dimensions.padding.top + dimensions.padding.bottom;
            }

            child_container.content.x = container_x;
            child_container.content.y = container_y;
            child_container.content.width = container_w;
            child_container.content.height = container_h;

            child->layout(child_container);

            std::string top_str = child->styled_node->value("top");
            std::string bottom_str = child->styled_node->value("bottom");
            std::string left_str = child->styled_node->value("left");
            std::string right_str = child->styled_node->value("right");

            if (!top_str.empty())
            {
                child->dimensions.content.y = container_y + LayoutEngine::parse_px(top_str, container_h) +
                                              child->dimensions.margin.top + child->dimensions.padding.top + child->dimensions.border.top;
            }
            else if (!bottom_str.empty())
            {
                child->dimensions.content.y = container_y + container_h - LayoutEngine::parse_px(bottom_str, container_h) -
                                              child->dimensions.margin.bottom - child->dimensions.padding.bottom - child->dimensions.border.bottom -
                                              child->dimensions.content.height;
            }

            if (!left_str.empty())
            {
                child->dimensions.content.x = container_x + LayoutEngine::parse_px(left_str, container_w) +
                                              child->dimensions.margin.left + child->dimensions.padding.left + child->dimensions.border.left;
            }
            else if (!right_str.empty())
            {
                child->dimensions.content.x = container_x + container_w - LayoutEngine::parse_px(right_str, container_w) -
                                              child->dimensions.margin.right - child->dimensions.padding.right - child->dimensions.border.right -
                                              child->dimensions.content.width;
            }

            Dimensions box_container;
            box_container.content.x = child->dimensions.content.x - child->dimensions.margin.left - child->dimensions.padding.left - child->dimensions.border.left;
            box_container.content.y = child->dimensions.content.y - child->dimensions.margin.top - child->dimensions.padding.top - child->dimensions.border.top;
            box_container.content.width = child->dimensions.content.width;
            box_container.content.height = child->dimensions.content.height;
            child->layout(box_container);

            float child_max_y = child->dimensions.content.y + child->total_content_height + child->dimensions.margin.bottom;
            float parent_min_y = dimensions.content.y - dimensions.padding.top - dimensions.border.top;
            max_total_h = std::max(max_total_h, child_max_y - parent_min_y);
        }
        
    }

} // namespace linweb
