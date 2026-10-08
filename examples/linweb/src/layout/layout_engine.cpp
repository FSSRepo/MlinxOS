#include "layout_engine.h"
#include "renderer/renderer.h"
#include <vector>
#include <memory>
#include <string>

namespace linweb
{

    /**
     * @brief Parses a string value to pixels, handling units such as %, px, rem and em.
     *
     * @param s The string to parse.
     * @param parent_size The size of the parent container for percentage or em calculations.
     * @return float The value converted to pixels.
     */
    float LayoutEngine::parse_px(const std::string &s, float parent_size)
    {
        if (s.empty())
            return 0;
        try
        {
            if (s.back() == '%')
            {
                float val = std::stof(s.substr(0, s.size() - 1));
                return (val / 100.0f) * parent_size;
            }
            size_t px = s.find("px");
            if (px != std::string::npos)
                return std::stof(s.substr(0, px));
            size_t vh = s.find("vh");
            if (vh != std::string::npos)
                return std::stof(s.substr(0, vh)) / 100.0f * (float)Renderer::get_viewport_height();
            size_t vw = s.find("vw");
            if (vw != std::string::npos)
                return std::stof(s.substr(0, vw)) / 100.0f * (float)Renderer::get_viewport_width();
            size_t rem = s.find("rem");
            if (rem != std::string::npos)
                return std::stof(s.substr(0, rem)) * 16.0f;
            size_t em = s.find("em");
            if (em != std::string::npos)
                return std::stof(s.substr(0, em)) * parent_size;
            return std::stof(s);
        }
        catch (...)
        {
            return 0;
        }
    }

    /**
     * @brief LayoutBox constructor. Determines the box type and initial position based on the style.
     *
     * @param node The styled node to position.
     */
    LayoutBox::LayoutBox(std::shared_ptr<StyledNode> node)
        : styled_node(std::move(node)), box_type(BoxType::Block), last_line_width(0), total_y_offset(0), scroll_y(0), z_index(0)
    {

        position = PositionType::Static;

        if (styled_node->node->type() == NodeType::Element)
        {
            std::string pos = styled_node->value("position");
            if (pos == "relative")
                position = PositionType::Relative;
            else if (pos == "absolute")
                position = PositionType::Absolute;
            else if (pos == "fixed")
                position = PositionType::Fixed;

            std::string display = styled_node->value("display");
            if (display == "inline")
            {
                box_type = BoxType::Inline;
            }
            else if (display == "inline-block")
            {
                box_type = BoxType::InlineBlock;
            }
            else if (display == "grid")
            {
                box_type = BoxType::Grid;
            }
            else if (display == "flex")
            {
                box_type = BoxType::Flex;
            }
            else
            {
                box_type = BoxType::Block;
            }

            std::string z_str = styled_node->value("z-index");
            if (!z_str.empty()) {
                try { z_index = std::stoi(z_str); } catch(...) { z_index = 0; }
            }
        }
        else
        {
            box_type = BoxType::Inline; // Text nodes are always inline
        }
    }

    /**
     * @brief Lays out the children, delegating according to the box type (Grid, Flex, Block).
     *
     * @param container_dimensions Container dimensions.
     */
    void LayoutBox::layout_children(Dimensions container_dimensions)
    {
        float current_x_offset = 0;
        float current_y_offset = 0;
        float max_line_height = 0;
        float max_x_offset = 0;

        // Filter children into normal flow and positioned
        std::vector<std::shared_ptr<LayoutBox>> normal_flow_children;
        for (const auto &child : children)
        {
            if (child->position != PositionType::Absolute && child->position != PositionType::Fixed)
            {
                normal_flow_children.push_back(child);
            }
        }

        if (box_type == BoxType::Grid)
        {
            layout_grid(container_dimensions, normal_flow_children);
        }
        else if (box_type == BoxType::Flex)
        {
            layout_flex(container_dimensions, normal_flow_children);
        }
        else
        {
            layout_block(container_dimensions, normal_flow_children);
        }
    }

    void LayoutBox::layout(Dimensions container_dimensions)
    {
        resolve_dimensions(container_dimensions);
        apply_relative_position(container_dimensions);
        layout_children(container_dimensions);
        layout_positioned_children();
    }

    std::shared_ptr<LayoutBox> LayoutEngine::build_layout_tree(const std::shared_ptr<StyledNode> &styled_node)
    {
        if (styled_node->value("display") == "none")
        {
            return nullptr;
        }

        auto root = std::make_shared<LayoutBox>(styled_node);

        // SVG elements are atomic in terms of layout tree; their children (raw text) shouldn't be laid out
        if (styled_node->node->tag_name == "svg")
        {
            return root;
        }

        for (const auto &child : styled_node->children)
        {
            auto layout_child = build_layout_tree(child);
            if (layout_child)
            {
                layout_child->parent = root;
                root->children.push_back(layout_child);
            }
        }

        return root;
    }

} // namespace linweb
