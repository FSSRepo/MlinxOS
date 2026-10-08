#include "layout_engine.h"
#include "../renderer/renderer.h"
#include "../renderer/texture_manager.h"
#include "../renderer/svg_loader.h"
#include "style/colors.h"
#include <algorithm>
#include <sstream>
#include <vector>
#include <memory>
#include <string>

namespace linweb
{

    /**
     * @brief Resolves the initial dimensions (margins, paddings, borders, width and height).
     *
     * @param container_dimensions The dimensions of the parent container.
     */
    void LayoutBox::resolve_dimensions(Dimensions container_dimensions)
    {
        if (styled_node->node->type() == NodeType::Element && styled_node->node->tag_name == "br") {
            dimensions = Dimensions();
            dimensions.content.x = container_dimensions.content.x;
            dimensions.content.y = container_dimensions.content.y;
            return;
        }

        float parent_width = container_dimensions.content.width;
        float parent_height = container_dimensions.content.height;

        float font_size = LayoutEngine::parse_px(styled_node->value("font-size"), 16.0f);
        if (font_size <= 0)
            font_size = 16.0f;

        std::string line_height_str = styled_node->value("line-height");
        if (!line_height_str.empty())
        {
            this->line_height = LayoutEngine::parse_px(line_height_str, font_size);
        }
        else
        {
            this->line_height = Renderer::get_line_height(font_size);
        }

        // 1. Resolve Margin, Padding, Border
        resolve_margin_padding_border(parent_width, parent_height, font_size);

        // 2. Resolve Width and Height
        bool is_pseudo = std::dynamic_pointer_cast<PseudoElementNode>(styled_node->node) != nullptr;
        if (styled_node->node->type() == NodeType::Text || is_pseudo)
        {
            resolve_text_node_dimensions(font_size, this->line_height);
        }
        else
        {
            resolve_content_width(container_dimensions, font_size);
        }

        // 3. Handle margin: auto for block elements
        if (box_type == BoxType::Block && styled_node->value("position") != "absolute" && styled_node->value("position") != "fixed")
        {
            float free_width = parent_width - (dimensions.content.width + dimensions.padding.left + dimensions.padding.right + dimensions.border.left + dimensions.border.right);
            if (free_width > 0)
            {
                bool left_auto = (dimensions.margin.left == -12345.0f);
                bool right_auto = (dimensions.margin.right == -12345.0f);

                if (left_auto && right_auto)
                {
                    dimensions.margin.left = free_width / 2.0f;
                    dimensions.margin.right = free_width / 2.0f;
                }
                else if (left_auto)
                {
                    dimensions.margin.left = free_width - ((dimensions.margin.right == -12345.0f) ? 0 : dimensions.margin.right);
                }
                else if (right_auto)
                {
                    dimensions.margin.right = free_width - ((dimensions.margin.left == -12345.0f) ? 0 : dimensions.margin.left);
                }
            }
        }

        // Treat remaining auto margins as 0
        if (dimensions.margin.left == -12345.0f)
            dimensions.margin.left = 0;
        if (dimensions.margin.right == -12345.0f)
            dimensions.margin.right = 0;
        if (dimensions.margin.top == -12345.0f)
            dimensions.margin.top = 0;
        if (dimensions.margin.bottom == -12345.0f)
            dimensions.margin.bottom = 0;

        // Initial position
        dimensions.content.x = container_dimensions.content.x + dimensions.margin.left + dimensions.padding.left + dimensions.border.left;
        dimensions.content.y = container_dimensions.content.y + dimensions.margin.top + dimensions.padding.top + dimensions.border.top;
    }

    /**
     * @brief Resolves the margins, paddings and borders of the box.
     *
     * @param parent_width Width of the parent container.
     * @param parent_height Height of the parent container.
     * @param font_size Current font size for relative units.
     */
    void LayoutBox::resolve_margin_padding_border(float parent_width, float parent_height, float font_size)
    {
        std::string tag = styled_node->node->tag_name;

        auto resolve_prop = [&](const std::string &prop, float parent_val) -> float
        {
            std::string val = styled_node->value(prop);
            if (val == "auto")
                return -12345.0f; // Sentinel for auto
            if (val.empty())
            {
                if (styled_node->value("margin").empty() && (prop.find("margin") == 0))
                {
                    if (prop == "margin-top" || prop == "margin-bottom")
                    {
                        if (tag == "h1")
                            return 0.67f * font_size;
                        if (tag == "h2")
                            return 0.83f * font_size;
                        if (tag == "h3" || tag == "p")
                            return 1.0f * font_size;
                    }
                }
                return 0.0f;
            }
            if (val.find("%") != std::string::npos)
                return LayoutEngine::parse_px(val, parent_val);
            return LayoutEngine::parse_px(val, font_size);
        };

        dimensions.margin.left = resolve_prop("margin-left", parent_width);
        dimensions.margin.right = resolve_prop("margin-right", parent_width);
        dimensions.margin.top = resolve_prop("margin-top", parent_width);
        dimensions.margin.bottom = resolve_prop("margin-bottom", parent_width);

        dimensions.padding.left = resolve_prop("padding-left", parent_width);
        dimensions.padding.right = resolve_prop("padding-right", parent_width);
        dimensions.padding.top = resolve_prop("padding-top", parent_width);
        dimensions.padding.bottom = resolve_prop("padding-bottom", parent_width);

        dimensions.border.left = resolve_prop("border-left-width", parent_width);
        dimensions.border.right = resolve_prop("border-right-width", parent_width);
        dimensions.border.top = resolve_prop("border-top-width", parent_width);
        dimensions.border.bottom = resolve_prop("border-bottom-width", parent_width);

        auto resolve_shorthand = [&](const std::string &prop, EdgeSizes &edges, bool is_margin)
        {
            std::string val = styled_node->value(prop);
            if (val.empty())
                return;

            std::vector<std::string> parts;
            std::stringstream ss(val);
            std::string part;
            while (ss >> part)
                parts.push_back(part);

            std::vector<float> values;
            for (const auto &p : parts)
            {
                if (p == "auto" && is_margin)
                    values.push_back(-12345.0f);
                else
                {
                    float ref = (prop == "margin" || prop == "padding") ? parent_width : font_size;
                    values.push_back(LayoutEngine::parse_px(p, ref));
                }
            }

            if (values.size() == 1)
            {
                edges = {values[0], values[0], values[0], values[0]};
            }
            else if (values.size() == 2)
            {
                edges.top = edges.bottom = values[0];
                edges.left = edges.right = values[1];
            }
            else if (values.size() == 3)
            {
                edges.top = values[0];
                edges.left = edges.right = values[1];
                edges.bottom = values[2];
            }
            else if (values.size() == 4)
            {
                edges.top = values[0];
                edges.right = values[1];
                edges.bottom = values[2];
                edges.left = values[3];
            }
        };

        resolve_shorthand("margin", dimensions.margin, true);
        resolve_shorthand("padding", dimensions.padding, false);

        std::string border_width = styled_node->value("border-width");
        if (!border_width.empty())
        {
            resolve_shorthand("border-width", dimensions.border, false);
        }

        for (int i = 0; i < 4; ++i)
        {
            dimensions.border_color.r[i] = 0;
            dimensions.border_color.g[i] = 0;
            dimensions.border_color.b[i] = 0;
            dimensions.border_color.a[i] = 1.0f;
        }

        std::string border = styled_node->value("border");
        if (!border.empty())
        {
            std::stringstream ss(border);
            std::string part;
            while (ss >> part)
            {
                if (part.find("px") != std::string::npos || part.find("em") != std::string::npos || (std::isdigit(part[0]) && part != "0"))
                {
                    float b = (part.find("%") != std::string::npos) ? LayoutEngine::parse_px(part, parent_width) : LayoutEngine::parse_px(part, font_size);
                    dimensions.border = {b, b, b, b};
                }
                else if (part != "solid" && part != "none" && part != "dashed" && part != "dotted")
                {
                    Color c = parse_color_str(part);
                    for (int i = 0; i < 4; ++i)
                    {
                        dimensions.border_color.r[i] = c.r;
                        dimensions.border_color.g[i] = c.g;
                        dimensions.border_color.b[i] = c.b;
                        dimensions.border_color.a[i] = c.a;
                    }
                }
            }
        }

        auto set_color = [&](int i, Color c)
        {
            dimensions.border_color.r[i] = c.r;
            dimensions.border_color.g[i] = c.g;
            dimensions.border_color.b[i] = c.b;
            dimensions.border_color.a[i] = c.a;
        };

        auto resolve_border_side = [&](const std::string &prop, int idx) {
            std::string val = styled_node->value(prop);
            if (val.empty()) return;
            std::stringstream ss(val);
            std::string part;
            while (ss >> part) {
                if (part.find("px") != std::string::npos || part.find("em") != std::string::npos || (std::isdigit(part[0]) && part != "0")) {
                    float b = (part.find("%") != std::string::npos) ? LayoutEngine::parse_px(part, parent_width) : LayoutEngine::parse_px(part, font_size);
                    if (idx == 0) dimensions.border.top = b;
                    else if (idx == 1) dimensions.border.right = b;
                    else if (idx == 2) dimensions.border.bottom = b;
                    else if (idx == 3) dimensions.border.left = b;
                } else if (part != "solid" && part != "none" && part != "dashed" && part != "dotted") {
                    Color c = parse_color_str(part);
                    set_color(idx, c);
                }
            }
        };

        resolve_border_side("border-top", 0);
        resolve_border_side("border-right", 1);
        resolve_border_side("border-bottom", 2);
        resolve_border_side("border-left", 3);

        std::string bc = styled_node->value("border-color");
        if (!bc.empty())
        {
            Color c = parse_color_str(bc);
            for (int i = 0; i < 4; ++i)
                set_color(i, c);
        }

        std::string btc = styled_node->value("border-top-color");
        if (!btc.empty())
            set_color(0, parse_color_str(btc));
        std::string brc = styled_node->value("border-right-color");
        if (!brc.empty())
            set_color(1, parse_color_str(brc));
        std::string bbc = styled_node->value("border-bottom-color");
        if (!bbc.empty())
            set_color(2, parse_color_str(bbc));
        std::string blc = styled_node->value("border-left-color");
        if (!blc.empty())
            set_color(3, parse_color_str(blc));
    }

    /**
     * @brief Calculates the dimensions for a text node.
     *
     * @param font_size Font size.
     * @param line_height Line height.
     */
    void LayoutBox::resolve_text_node_dimensions(float font_size, float line_height)
    {
        std::string text_data;
        if (auto text_node = std::dynamic_pointer_cast<TextNode>(styled_node->node)) {
            text_data = text_node->data;
        } else if (auto pseudo_node = std::dynamic_pointer_cast<PseudoElementNode>(styled_node->node)) {
            text_data = pseudo_node->pseudo_content;
        }
        std::string font_weight = styled_node->value("font-weight");
        bool is_bold = (font_weight == "bold" || font_weight == "700" || font_weight == "800" || font_weight == "900");
        std::string font_family = styled_node->value("font-family");
        bool is_monospace = (font_family == "monospace");

        dimensions.content.width = Renderer::measure_text(text_data, font_size, is_bold, is_monospace);
        dimensions.content.height = line_height;
    }

    /**
     * @brief Calculates the content width of the box.
     *
     * @param container_dimensions Container dimensions.
     * @param font_size Current font size.
     */
    void LayoutBox::resolve_content_width(Dimensions container_dimensions, float font_size)
    {
        float parent_width = container_dimensions.content.width;
        std::string box_sizing = styled_node->value("box-sizing");
        bool is_border_box = (box_sizing == "border-box");

        std::string w_str = styled_node->value("width");
        if (w_str.empty() && styled_node->node->tag_name == "img") {
            auto it = styled_node->node->data.attributes.find("width");
            if (it != styled_node->node->data.attributes.end()) {
                w_str = it->second;
                if (std::isdigit(w_str[0])) w_str += "px";
            }
        }

        if (!w_str.empty())
        {
            float w = (w_str.find("%") != std::string::npos) ? LayoutEngine::parse_px(w_str, parent_width) : LayoutEngine::parse_px(w_str, font_size);
            if (is_border_box)
            {
                w -= (dimensions.padding.left + dimensions.padding.right +
                      dimensions.border.left + dimensions.border.right);
            }
            dimensions.content.width = std::max(0.0f, w);
        }
        else if ((box_type == BoxType::Block || box_type == BoxType::Grid || box_type == BoxType::Flex) &&
                 styled_node->value("position") != "absolute" &&
                 styled_node->value("position") != "fixed")
        {
            auto p = parent.lock();
            bool is_flex_item = p && p->box_type == BoxType::Flex;
            bool should_stretch = true;
            if (is_flex_item)
            {
                std::string flex_dir = p->styled_node->value("flex-direction");
                std::string align_items = p->styled_node->value("align-items");
                if (flex_dir == "column")
                {
                    if (align_items != "stretch" && !align_items.empty())
                        should_stretch = false;
                }
                else
                {
                    should_stretch = false;
                }
            }

            if (should_stretch)
            {
                float ml = (dimensions.margin.left == -12345.0f) ? 0 : dimensions.margin.left;
                float mr = (dimensions.margin.right == -12345.0f) ? 0 : dimensions.margin.right;
                dimensions.content.width = std::max(0.0f, parent_width - ml - mr -
                                                              dimensions.padding.left - dimensions.padding.right -
                                                              dimensions.border.left - dimensions.border.right);
            }
            else
            {
                dimensions.content.width = 0;
            }
        }
        else
        {
            dimensions.content.width = 0;
        }

        std::string min_w_str = styled_node->value("min-width");
        if (!min_w_str.empty())
        {
            float min_w = LayoutEngine::parse_px(min_w_str, parent_width);
            if (is_border_box)
            {
                min_w -= (dimensions.padding.left + dimensions.padding.right +
                          dimensions.border.left + dimensions.border.right);
            }
            if (dimensions.content.width < min_w)
            {
                dimensions.content.width = std::max(0.0f, min_w);
            }
        }
        
        // Handle image intrinsic width if not set
        if ((styled_node->node->tag_name == "img" || styled_node->node->tag_name == "svg") && w_str.empty()) {
            std::string src = styled_node->node->data.src();
            if (styled_node->node->tag_name == "svg") {
                std::string content = SvgLoader::reconstruct_svg(styled_node->node);
                float sw = 0, sh = 0;
                if (SvgLoader::get_intrinsic_size_from_string(content, sw, sh)) {
                    dimensions.content.width = sw;
                }
            } else if (!src.empty()) {
                if (src.size() > 4 && src.substr(src.size() - 4) == ".svg") {
                    float sw = 0, sh = 0;
                    if (SvgLoader::get_intrinsic_size(src, sw, sh)) {
                        dimensions.content.width = sw;
                    }
                } else {
                    Texture tex = TextureManager::get().get_texture(src);
                    if (tex.id != 0) {
                        dimensions.content.width = static_cast<float>(tex.width);
                    }
                }
            }
        }
    }

    /**
     * @brief Calculates the final height of the box based on its content or specified properties.
     *
     * @param container_dimensions Container dimensions.
     * @param current_y_offset Final Y offset of the content.
     * @param max_line_height Height of the last line.
     * @param max_x_offset Maximum width reached by the content.
     * @param current_x_offset Final X offset.
     */
    void LayoutBox::resolve_height(Dimensions container_dimensions, float current_y_offset, float max_line_height, float max_x_offset, float current_x_offset)
    {
        if (styled_node->node->type() == NodeType::Element && styled_node->node->tag_name == "br") {
            dimensions.content.height = 0;
            total_content_height = 0;
            last_line_width = 0;
            total_y_offset = 0;
            return;
        }

        float parent_height = container_dimensions.content.height;
        std::string w_str = styled_node->value("width");
        std::string h_str = styled_node->value("height");
        if (h_str.empty() && styled_node->node->tag_name == "img") {
            auto it = styled_node->node->data.attributes.find("height");
            if (it != styled_node->node->data.attributes.end()) {
                h_str = it->second;
                if (std::isdigit(h_str[0])) h_str += "px";
            }
        }

        std::string min_h_str = styled_node->value("min-height");
        std::string max_h_str = styled_node->value("max-height");

        float children_height = (children.empty() && styled_node->node->type() == NodeType::Text) ? this->line_height : (current_y_offset + max_line_height);

        // Add padding and border to total_content_height for scrollable areas
        total_content_height = children_height + dimensions.padding.top + dimensions.padding.bottom + dimensions.border.top + dimensions.border.bottom;

        bool is_border_box = styled_node->value("box-sizing") == "border-box";

        // Detect if parent height is an indefinite sentinel value (safety net)
        bool parent_height_indefinite = (parent_height >= 999990.0f);

        if (!h_str.empty())
        {
            bool is_pct = (h_str.find('%') != std::string::npos);
            if (is_pct && parent_height_indefinite) {
                // Per CSS spec: percentage height = auto when parent has indefinite height
                dimensions.content.height = std::max(0.0f, children_height);
            } else {
                float h = LayoutEngine::parse_px(h_str, parent_height);
                if (is_border_box)
                {
                    h -= (dimensions.padding.top + dimensions.padding.bottom +
                          dimensions.border.top + dimensions.border.bottom);
                }
                dimensions.content.height = std::max(0.0f, h);
            }
        }
        else
        {
            dimensions.content.height = std::max(0.0f, children_height);
        }

        // Apply max-height
        if (!max_h_str.empty())
        {
            bool is_pct = (max_h_str.find('%') != std::string::npos);
            if (!(is_pct && parent_height_indefinite)) {
                float max_h = LayoutEngine::parse_px(max_h_str, parent_height);
                if (is_border_box)
                {
                    max_h -= (dimensions.padding.top + dimensions.padding.bottom +
                              dimensions.border.top + dimensions.border.bottom);
                }
                if (dimensions.content.height > max_h)
                {
                    dimensions.content.height = std::max(0.0f, max_h);
                }
            }
        }

        // Apply min-height
        if (!min_h_str.empty())
        {
            bool is_pct = (min_h_str.find('%') != std::string::npos);
            if (!(is_pct && parent_height_indefinite)) {
                float min_h = LayoutEngine::parse_px(min_h_str, parent_height);
                if (is_border_box)
                {
                    min_h -= (dimensions.padding.top + dimensions.padding.bottom +
                              dimensions.border.top + dimensions.border.bottom);
                }
                if (dimensions.content.height < min_h)
                {
                    dimensions.content.height = std::max(0.0f, min_h);
                }
            }
        }

        // Handle image intrinsic height and aspect ratio if not set
        if (styled_node->node->tag_name == "img" || styled_node->node->tag_name == "svg") {
            std::string src = styled_node->node->data.src();
            float intrinsic_w = 0;
            float intrinsic_h = 0;
            bool found = false;

            if (styled_node->node->tag_name == "svg") {
                std::string content = SvgLoader::reconstruct_svg(styled_node->node);
                found = SvgLoader::get_intrinsic_size_from_string(content, intrinsic_w, intrinsic_h);
            } else if (!src.empty()) {
                if (src.size() > 4 && src.substr(src.size() - 4) == ".svg") {
                    found = SvgLoader::get_intrinsic_size(src, intrinsic_w, intrinsic_h);
                } else {
                    Texture tex = TextureManager::get().get_texture(src);
                    if (tex.id != 0) {
                        intrinsic_w = static_cast<float>(tex.width);
                        intrinsic_h = static_cast<float>(tex.height);
                        found = true;
                    }
                }
            }

            if (found && intrinsic_w > 0 && intrinsic_h > 0) {
                if (h_str.empty()) {
                    if (!w_str.empty()) {
                        // Maintain aspect ratio based on set width
                        dimensions.content.height = (dimensions.content.width / intrinsic_w) * intrinsic_h;
                    } else {
                        dimensions.content.height = intrinsic_h;
                    }
                } else if (w_str.empty()) {
                    // Maintain aspect ratio based on set height
                    dimensions.content.width = (dimensions.content.height / intrinsic_h) * intrinsic_w;
                }
            }
        }

        // Adjust for inline width/height if not specified
        bool is_abs_fixed = styled_node->value("position") == "absolute" || styled_node->value("position") == "fixed";

        bool is_replaced_element = styled_node->node->tag_name == "img";
        bool should_inline_shrink = (box_type == BoxType::Inline || box_type == BoxType::InlineBlock) && !is_replaced_element;

        if (w_str.empty() && (dimensions.content.width <= 0.001f || should_inline_shrink || is_abs_fixed))
        {
            if (styled_node->node->type() != NodeType::Text)
            {
                if (!is_replaced_element || dimensions.content.width <= 0.001f)
                {
                    dimensions.content.width = max_x_offset;
                }
            }

            // For pure inline, height should be at least line height if it has content
            if (box_type == BoxType::Inline && dimensions.content.height <= 0.001f && !children.empty())
            {
                dimensions.content.height = line_height;
            }
        }

        this->last_line_width = current_x_offset;
        this->total_y_offset = current_y_offset;

        // Calculate total content height including children for scroll limits
        if (box_type != BoxType::Inline)
        {
            // Initial range covers our own content area plus padding and borders
            float min_y = dimensions.content.y - dimensions.padding.top - dimensions.border.top;
            float max_y = dimensions.content.y + dimensions.content.height + dimensions.padding.bottom + dimensions.border.bottom;

            for (const auto &child : children)
            {
                if (child->position == PositionType::Static || child->position == PositionType::Relative)
                {
                    // Use the child's total content height which already includes its own children and margins
                    float child_min_y = child->dimensions.content.y - child->dimensions.padding.top - child->dimensions.border.top;
                    float child_max_y = child_min_y + child->total_content_height;

                    // Include child's margins in the parent's content height
                    float child_outer_top = child_min_y - child->dimensions.margin.top;
                    float child_outer_bottom = child_max_y + child->dimensions.margin.bottom;

                    min_y = std::min(min_y, child_outer_top);
                    max_y = std::max(max_y, child_outer_bottom);
                }
            }
            total_content_height = max_y - min_y;
        }
        else
        {
            total_content_height = dimensions.content.height;
        }

        // Final special cases for html/body
        if (styled_node->node->type() == NodeType::Element)
        {
            auto element = std::static_pointer_cast<ElementNode>(styled_node->node);
            if (element->tag_name == "html")
            {
                float target_height = container_dimensions.content.height;
                float viewport_height = (float)Renderer::get_viewport_height();

                if (target_height <= 0)
                {
                    target_height = viewport_height;
                }

                std::string h_str = styled_node->value("height");
                std::string min_h_str = styled_node->value("min-height");
                
                // Only force viewport height if explicitly requested or if we want to ensure 
                // the root element covers at least the viewport (common expectation)
                // But we should be careful about margins.
                
                std::string overflow = styled_node->value("overflow");

                if (overflow == "auto" || overflow == "scroll" || overflow == "hidden")
                {
                    // If it's the root scroll container, it should probably be the size of the viewport
                    dimensions.content.height = std::min(dimensions.content.height, target_height);
                }
            }
        }
    }

} // namespace linweb
