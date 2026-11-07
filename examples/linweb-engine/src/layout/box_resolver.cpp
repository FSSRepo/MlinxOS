#include "layout_engine.h"
#include "utils/text_transform.h"
#include "utils/text_wrap.h"
#include "../renderer/renderer.h"
#include "../renderer/texture_manager.h"
#include "../renderer/svg_loader.h"
#include "../renderer/custom_font.h"
#include "style/colors.h"
#include "forms/form_controls.h"
#include "utils/control_label.h"
#include "utils/file_utils.h"
#include "media/video_player.h"
#include <algorithm>
#include <sstream>
#include <vector>
#include <memory>
#include <string>

namespace linweb
{
    namespace
    {
        // Size of the picture of a <video>: the video being played, or the file its src points to.
        bool video_intrinsic_size(const std::string &src, float &width, float &height)
        {
            int w = 0, h = 0;
            if (src.empty() || !video::dimensions(src.compare(0, 4, "http") == 0 ? src : resolve_image_src(src), w, h))
            {
                return false;
            }
            width = static_cast<float>(w);
            height = static_cast<float>(h);
            return w > 0 && h > 0;
        }
    } // namespace

    namespace
    {
        // The box is the first thing in the flow of its parent: nothing but white space and empty blocks comes before it
        bool starts_flow_of_parent(const LayoutBox &box)
        {
            auto parent_box = box.parent.lock();
            if (!parent_box) return false;
            for (const auto &sibling : parent_box->children) {
                if (sibling->styled_node->node == box.styled_node->node) return true;
                if (sibling->styled_node->node->type() == NodeType::Text) {
                    if (static_cast<TextNode &>(*sibling->styled_node->node).data.find_first_not_of(" \t\n\r\f") != std::string::npos) return false;
                    continue;
                }
                const Dimensions &d = sibling->dimensions;
                const bool empty_block = sibling->laid_out && d.content.height == 0.0f && d.padding.top == 0.0f && d.padding.bottom == 0.0f &&
                                         d.border.top == 0.0f && d.border.bottom == 0.0f && d.margin.top == 0.0f && d.margin.bottom == 0.0f;
                if (!empty_block) return false;
            }
            return false;
        }

        // Quirks mode drops the user agent margin above the first block of a body or a table cell, and also when the
        // blocks around it have nothing between that margin and the container (no padding, border or formatting context)
        bool quirk_drops_top_margin(const LayoutBox &box)
        {
            auto parent_box = box.parent.lock();
            if (!parent_box || !starts_flow_of_parent(box)) return false;
            const std::string &tag = parent_box->styled_node->node->tag_name;
            if (tag == "body" || tag == "td" || tag == "th") return true;
            auto outer = parent_box->parent.lock();
            const std::string overflow = parent_box->styled_node->value("overflow");
            const std::string position = parent_box->styled_node->value("position");
            const bool own_context = (outer && (outer->box_type == BoxType::Flex || outer->box_type == BoxType::Grid)) ||
                                     (!overflow.empty() && overflow != "visible") || position == "absolute" || position == "fixed";
            if (parent_box->box_type != BoxType::Block || own_context || parent_box->dimensions.padding.top != 0.0f ||
                parent_box->dimensions.border.top != 0.0f)
                return false;
            return quirk_drops_top_margin(*parent_box);
        }

        // The words of a shorthand value; a function such as rgba(255, 255, 255, 0.2) stays one word
        std::vector<std::string> value_words(const std::string &value)
        {
            std::vector<std::string> words;
            std::string current;
            int depth = 0;
            for (char c : value)
            {
                if (c == '(') ++depth;
                else if (c == ')' && depth > 0) --depth;
                if (depth == 0 && std::isspace(static_cast<unsigned char>(c)))
                {
                    if (!current.empty()) words.push_back(current);
                    current.clear();
                }
                else current += c;
            }
            if (!current.empty()) words.push_back(current);
            return words;
        }

        // Default size of the content box of a form control (a CSS width or height wins over it).
        bool control_intrinsic_size(const Node &node, float font_size, float &width, float &height)
        {
            if (is_button_input(node)) {
                width = Renderer::measure_text(button_input_label(node), font_size);
                height = Renderer::get_line_height(font_size);
                return true;
            }
            switch (forms::kind_of(node))
            {
            case forms::Kind::Text:
            {
                long chars = std::strtol(node.data.attributes.count("size") ? node.data.attributes.at("size").c_str() : "20", nullptr, 10);
                if (chars <= 0) chars = 20;
                width = static_cast<float>(chars) * Renderer::measure_text("0", font_size) + 1.56f * font_size;
                height = Renderer::get_line_height(font_size);
                return true;
            }
            case forms::Kind::Range:
                width = 129.0f;
                height = 16.0f;
                return true;
            case forms::Kind::Progress:
                width = 160.0f;
                height = 16.0f;
                return true;
            case forms::Kind::Select:
            {
                // as wide as its longest option, plus the 6px inset the list draws its text with on each side
                width = 0.0f;
                for (const auto& option : forms::options_of(node)) {
                    width = std::max(width, Renderer::measure_text(forms::option_text(*option), font_size));
                }
                height = Renderer::get_line_height(font_size);
                return true;
            }
            case forms::Kind::None:
                break;
            }
            return false;
        }
    } // namespace

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
        if (!line_height_str.empty() && line_height_str != "normal")
        {
            this->line_height = LayoutEngine::parse_px(line_height_str, font_size);
            if (line_height_str.find_first_not_of("0123456789. ") == std::string::npos) this->line_height *= font_size;
        }
        else
        {
            this->line_height = Renderer::get_line_height(font_size, CustomFonts::select(styled_node->value("font-family"), styled_node->value("font-weight"), styled_node->value("font-style")));
        }

        // 1. Resolve Margin, Padding, Border
        resolve_margin_padding_border(parent_width, parent_height, font_size);

        // 2. Resolve Width and Height
        bool is_pseudo = std::dynamic_pointer_cast<PseudoElementNode>(styled_node->node) != nullptr;
        if (styled_node->node->type() == NodeType::Text || is_pseudo)
        {
            resolve_text_node_dimensions(container_dimensions, font_size, this->line_height);
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

        ua_margin_top = false;
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
                        if (prop == "margin-top" && (tag == "h1" || tag == "h2" || tag == "h3" || tag == "p"))
                            ua_margin_top = true;
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

        // Quirks mode: the user agent margin of the first block of the body (or of a table cell), also through the blocks
        // that start it, is dropped
        if (g_quirks_mode && ua_margin_top && quirk_drops_top_margin(*this)) dimensions.margin.top = 0.0f;

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
                    // a percentage is of the containing width, but an em is of the element's own font size
                    bool is_em = p.size() > 2 && p.compare(p.size() - 2, 2, "em") == 0;
                    float ref = ((prop == "margin" || prop == "padding") && !is_em) ? parent_width : font_size;
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

        // a side set by its own longhand wins over the shorthand (margin: 2px from a UA default must not undo margin-left)
        auto refine = [&](const std::string &prop, float &field, float parent_val)
        {
            if (!styled_node->value(prop).empty())
                field = resolve_prop(prop, parent_val);
        };
        refine("margin-left", dimensions.margin.left, parent_width);
        refine("margin-right", dimensions.margin.right, parent_width);
        refine("margin-top", dimensions.margin.top, parent_width);
        refine("margin-bottom", dimensions.margin.bottom, parent_width);
        refine("padding-left", dimensions.padding.left, parent_width);
        refine("padding-right", dimensions.padding.right, parent_width);
        refine("padding-top", dimensions.padding.top, parent_width);
        refine("padding-bottom", dimensions.padding.bottom, parent_width);

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
            for (const std::string &part : value_words(border))
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
            for (const std::string &part : value_words(val)) {
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

    std::string LayoutBox::text_to_show(float* leading_trim) const
    {
        if (leading_trim) *leading_trim = 0;
        std::string text_data;
        if (auto text_node = std::dynamic_pointer_cast<TextNode>(styled_node->node)) {
            text_data = apply_text_transform(text_node->data, styled_node->value("text-transform"));
        } else if (auto pseudo_node = std::dynamic_pointer_cast<PseudoElementNode>(styled_node->node)) {
            text_data = pseudo_node->pseudo_content;
        }

        // A text node that opens or closes a block drops the white space at that edge, as a line does in CSS.
        // Its leading part is kept out of the width, and drawing moves the text left by it.
        auto parent_box = parent.lock();
        auto dom_node = styled_node->node;
        auto dom_parent = dom_node->parent.lock();
        bool is_text = std::dynamic_pointer_cast<TextNode>(dom_node) != nullptr;
        if (is_text && parent_box && parent_box->box_type != BoxType::Inline && dom_parent) {
            auto blank = [](const std::shared_ptr<Node>& n) {
                auto t = std::dynamic_pointer_cast<TextNode>(n);
                return t && t->data.find_first_not_of(" \t\n\r\f") == std::string::npos;
            };
            const auto& siblings = dom_parent->children;
            bool at_start = true, at_end = true;
            for (const auto& sibling : siblings) {
                if (sibling == dom_node) break;
                if (!blank(sibling)) at_start = false;
            }
            for (auto it = siblings.rbegin(); it != siblings.rend() && *it != dom_node; ++it) {
                if (!blank(*it)) at_end = false;
            }
            size_t first = text_data.find_first_not_of(" \t\n\r\f");
            size_t last = text_data.find_last_not_of(" \t\n\r\f");
            if (first == std::string::npos) {
                // a blank node between two boxes is a real space; only at a block edge does it disappear
                if (at_start || at_end) text_data.clear();
            } else {
                if (at_end) text_data.erase(last + 1);
                if (at_start && first > 0) {
                    if (leading_trim) *leading_trim = measure_text_run(text_data.substr(0, first));
                    text_data.erase(0, first);
                }
            }
        }
        return text_data;
    }

    float LayoutBox::measure_text_run(const std::string& text) const
    {
        float font_size = LayoutEngine::parse_px(styled_node->value("font-size"), 16.0f);
        if (font_size <= 0) font_size = 16.0f;
        std::string font_weight = styled_node->value("font-weight");
        bool is_bold = (font_weight == "bold" || font_weight == "700" || font_weight == "800" || font_weight == "900");
        std::string font_family = styled_node->value("font-family");
        bool is_monospace = (font_family == "monospace");
        const CustomFont* custom = CustomFonts::select(font_family, font_weight, styled_node->value("font-style"));
        return Renderer::measure_text(text, font_size, is_bold, is_monospace, custom)
            + parse_letter_spacing(styled_node->value("letter-spacing")) * static_cast<float>(utf8_char_count(text));
    }

    // The nearest block around the text: the lines after the first start at its left edge and are as wide as it is
    static std::shared_ptr<LayoutBox> block_around(const LayoutBox& box)
    {
        // an inline box that is an item of a flex or grid container is a block of its own
        auto is_item = [](const std::shared_ptr<LayoutBox>& candidate) {
            auto outer = candidate->parent.lock();
            return outer && (outer->box_type == BoxType::Flex || outer->box_type == BoxType::Grid);
        };
        auto ancestor = box.parent.lock();
        while (ancestor && (ancestor->box_type == BoxType::Inline || ancestor->box_type == BoxType::Anonymous) && !is_item(ancestor))
            ancestor = ancestor->parent.lock();
        return ancestor;
    }

    static float block_width_around(const LayoutBox& box)
    {
        auto block = block_around(box);
        return block ? block->dimensions.content.width : 0.0f;
    }

    float LayoutBox::text_block_left() const
    {
        auto block = block_around(*this);
        return block ? block->dimensions.content.x : dimensions.content.x;
    }

    // Where a line of a wrapped text starts, with the block's text-align applied. The first line only moves when the
    // text starts its line; the box's own position already holds the lines that come before it.
    float LayoutBox::text_line_x(size_t index, float line_width) const
    {
        auto block = block_around(*this);
        if (!block) return dimensions.content.x - text_leading_trim;
        const std::string align = styled_node->value("text-align");
        const float factor = align == "center" ? 0.5f : (align == "right" || align == "end") ? 1.0f : 0.0f;
        const float left = block->dimensions.content.x;
        const bool starts_line = text_first_width >= block->dimensions.content.width - 0.5f;
        if (index == 0 && (factor == 0.0f || !starts_line))
            return dimensions.content.x - text_leading_trim;
        return left + std::max(0.0f, block->dimensions.content.width - line_width) * factor;
    }

    std::vector<std::string> LayoutBox::text_lines() const
    {
        std::string text = text_to_show();
        const std::string white_space = styled_node->value("white-space");
        // A text in an inline-level item of a flex or grid container (a span with a margin, a size label) is sized by its
        // content: the item is as wide as its text, so the text never wraps there.
        if (auto block = block_around(*this); block && block->box_type == BoxType::Inline) return {text};
        const float later_width = block_width_around(*this);
        if (white_space == "nowrap" || white_space.compare(0, 3, "pre") == 0 || text.find(' ') == std::string::npos ||
            text_first_width <= 1.0f || later_width <= 1.0f)
            return {text};
        return wrap_text(text, text_first_width, later_width, [this](const std::string& run) { return measure_text_run(run); });
    }

    /**
     * @brief Calculates the dimensions for a text node.
     *
     * @param container_dimensions Container: its width is what is left on the line the text starts on.
     * @param font_size Font size.
     * @param line_height Line height.
     */
    void LayoutBox::resolve_text_node_dimensions(Dimensions container_dimensions, float font_size, float line_height)
    {
        std::string text_data = text_to_show(&text_leading_trim);

        // A box that is only moved (line alignment, flex placement) is laid out again in a container as wide as itself:
        // its wrapping stays as the flow of its line found it.
        const bool moved_only = laid_out && text_line_count > 1 && container_dimensions.content.width == dimensions.content.width;
        if (!moved_only) text_first_width = container_dimensions.content.width;
        text_line_count = 1;
        total_y_offset = 0;
        last_line_width = 0;
        dimensions.content.width = measure_text_run(text_data);
        dimensions.content.height = line_height;

        std::vector<std::string> lines = text_lines();
        if (lines.size() > 1) {
            float widest = 0;
            for (const auto& line : lines) widest = std::max(widest, measure_text_run(line));
            text_line_count = static_cast<int>(lines.size());
            dimensions.content.width = widest;
            dimensions.content.height = line_height * static_cast<float>(lines.size());
            total_y_offset = line_height * static_cast<float>(lines.size() - 1);
            last_line_width = measure_text_run(lines.back());
        }
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
        if (w_str == "auto") w_str.clear();
        if (w_str.empty() && (styled_node->node->tag_name == "img" || styled_node->node->tag_name == "svg" || styled_node->node->tag_name == "video")) {
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
        else if ((styled_node->value("position") == "absolute" || styled_node->value("position") == "fixed") &&
                 !styled_node->value("left").empty() && !styled_node->value("right").empty())
        {
            // An absolutely positioned box with auto width and both left and right set fills what those leave
            // the containing block, not this box: a second layout of the box passes the box's own width as its container
            float block_width = containing_block_width > 0 ? containing_block_width : parent_width;
            float l = LayoutEngine::parse_px(styled_node->value("left"), block_width);
            float r = LayoutEngine::parse_px(styled_node->value("right"), block_width);
            float ml = (dimensions.margin.left == -12345.0f) ? 0 : dimensions.margin.left;
            float mr = (dimensions.margin.right == -12345.0f) ? 0 : dimensions.margin.right;
            dimensions.content.width = std::max(0.0f, block_width - l - r - ml - mr -
                                                          dimensions.padding.left - dimensions.padding.right -
                                                          dimensions.border.left - dimensions.border.right);
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

        // max-width caps the width; min-width is applied after it, so it wins when the two conflict (as in CSS)
        std::string max_w_str = styled_node->value("max-width");
        if (!max_w_str.empty() && max_w_str != "none")
        {
            float max_w = LayoutEngine::parse_px(max_w_str, max_w_str.find('%') != std::string::npos ? parent_width : font_size);
            if (is_border_box)
            {
                max_w -= (dimensions.padding.left + dimensions.padding.right +
                          dimensions.border.left + dimensions.border.right);
            }
            if (dimensions.content.width > max_w)
            {
                dimensions.content.width = std::max(0.0f, max_w);
            }
        }

        std::string min_w_str = styled_node->value("min-width");
        if (!min_w_str.empty())
        {
            float min_w = LayoutEngine::parse_px(min_w_str, min_w_str.find('%') != std::string::npos ? parent_width : font_size);
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
        
        // A form control without a width gets its default one
        if (w_str.empty()) {
            float control_w = 0, control_h = 0;
            if (control_intrinsic_size(*styled_node->node, font_size, control_w, control_h)) {
                dimensions.content.width = control_w;
            }
        }

        // A <video> without a width is as wide as its picture (300 px, like a browser, while it has none)
        if (styled_node->node->tag_name == "video" && w_str.empty()) {
            float video_w = 0, video_h = 0;
            dimensions.content.width = video_intrinsic_size(styled_node->node->data.src(), video_w, video_h) ? video_w : 300.0f;
        }

        // Handle image intrinsic width if not set
        if ((styled_node->node->tag_name == "img" || styled_node->node->tag_name == "svg") && w_str.empty()) {
            std::string src = resolve_image_src(styled_node->node->data.src());
            if (styled_node->node->tag_name == "svg") {
                std::string content = SvgLoader::reconstruct_svg(styled_node->node);
                float sw = 0, sh = 0;
                if (SvgLoader::get_intrinsic_size_from_string(content, sw, sh)) {
                    // without a size of its own an svg fills the width its parent has set, or is 300px as in a browser;
                    // its viewBox only gives the proportions (see the height below)
                    auto parent_box = parent.lock();
                    float parent_width = (parent_box && !parent_box->styled_node->value("width").empty()) ? parent_box->dimensions.content.width : 0.0f;
                    dimensions.content.width = parent_width > 0.001f ? parent_width : 300.0f;
                }
            } else if (!src.empty()) {
                if (src.size() > 4 && src.substr(src.size() - 4) == ".svg") {
                    float sw = 0, sh = 0;
                    if (SvgLoader::get_intrinsic_size(src, sw, sh)) {
                        dimensions.content.width = sw;
                    }
                } else {
                    int image_w = 0, image_h = 0;
                    if (TextureManager::get().image_size(src, image_w, image_h)) {
                        dimensions.content.width = static_cast<float>(image_w);
                    }
                }
            }
        }
    }

    void LayoutBox::update_total_content_height()
    {
        if (box_type == BoxType::Inline)
        {
            total_content_height = dimensions.content.height;
            return;
        }

        // Initial range covers our own content area plus padding and borders
        float min_y = dimensions.content.y - dimensions.padding.top - dimensions.border.top;
        float max_y = dimensions.content.y + dimensions.content.height + dimensions.padding.bottom + dimensions.border.bottom;

        for (const auto &child : children)
        {
            // A box layout never reached (the white space a flex or grid container skips) keeps the coordinates it was
            // created with, and a line break has no painted box. Neither can extend the scroll bounds.
            if (!child->laid_out) continue;
            if (child->styled_node->node->tag_name == "br") continue;
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
        if (w_str == "auto") w_str.clear();
        if (h_str == "auto") h_str.clear();
        if (h_str.empty() && (styled_node->node->tag_name == "img" || styled_node->node->tag_name == "svg" || styled_node->node->tag_name == "video")) {
            auto it = styled_node->node->data.attributes.find("height");
            if (it != styled_node->node->data.attributes.end()) {
                h_str = it->second;
                if (std::isdigit(h_str[0])) h_str += "px";
            }
        }

        std::string min_h_str = styled_node->value("min-height");
        std::string max_h_str = styled_node->value("max-height");

        float children_height = (children.empty() && (styled_node->node->type() == NodeType::Text || text_line_count > 1)) ? this->line_height * static_cast<float>(std::max(1, text_line_count)) : (current_y_offset + max_line_height);

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
                // A percentage refers to the parent's height. A parent with a px height of its own is not laid out
                // yet, so its height comes from its style, not from the provisional height passed in.
                float percent_base = parent_height;
                if (auto parent_box = parent.lock()) {
                    std::string parent_h = parent_box->styled_node->value("height");
                    if (!parent_h.empty() && parent_h != "auto" && parent_h.find('%') == std::string::npos) {
                        percent_base = LayoutEngine::parse_px(parent_h, parent_height);
                        if (parent_box->styled_node->value("box-sizing") == "border-box") {
                            percent_base -= (parent_box->dimensions.padding.top + parent_box->dimensions.padding.bottom +
                                             parent_box->dimensions.border.top + parent_box->dimensions.border.bottom);
                        }
                    }
                }
                float h = LayoutEngine::parse_px(h_str, h_str.find('%') != std::string::npos ? percent_base : LayoutEngine::parse_px(styled_node->value("font-size"), 16));
                if (is_border_box)
                {
                    h -= (dimensions.padding.top + dimensions.padding.bottom +
                          dimensions.border.top + dimensions.border.bottom);
                }
                dimensions.content.height = std::max(0.0f, h);
            }
        }
        else if ((styled_node->value("position") == "absolute" || styled_node->value("position") == "fixed") &&
                 !styled_node->value("top").empty() && !styled_node->value("bottom").empty() && !parent_height_indefinite)
        {
            // Auto height with both top and bottom set fills what those leave
            float block_height = containing_block_height > 0 ? containing_block_height : parent_height;
            float t = LayoutEngine::parse_px(styled_node->value("top"), block_height);
            float b = LayoutEngine::parse_px(styled_node->value("bottom"), block_height);
            float mt = (dimensions.margin.top == -12345.0f) ? 0 : dimensions.margin.top;
            float mb = (dimensions.margin.bottom == -12345.0f) ? 0 : dimensions.margin.bottom;
            dimensions.content.height = std::max(0.0f, block_height - t - b - mt - mb -
                                                         dimensions.padding.top - dimensions.padding.bottom -
                                                         dimensions.border.top - dimensions.border.bottom);
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
                float max_h = LayoutEngine::parse_px(max_h_str, is_pct ? parent_height : LayoutEngine::parse_px(styled_node->value("font-size"), 16));
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
                float min_h = LayoutEngine::parse_px(min_h_str, is_pct ? parent_height : LayoutEngine::parse_px(styled_node->value("font-size"), 16));
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

        // A <video> keeps the aspect ratio of its picture when only one side is set
        if (styled_node->node->tag_name == "video") {
            float video_w = 0, video_h = 0;
            if (video_intrinsic_size(styled_node->node->data.src(), video_w, video_h)) {
                if (h_str.empty()) {
                    dimensions.content.height = w_str.empty() ? video_h : (dimensions.content.width / video_w) * video_h;
                } else if (w_str.empty()) {
                    dimensions.content.width = (dimensions.content.height / video_h) * video_w;
                }
            } else if (h_str.empty()) {
                dimensions.content.height = 150.0f;
            }
        }

        // A form control without a height gets its default one
        if (h_str.empty()) {
            float control_w = 0, control_h = 0;
            float control_font = LayoutEngine::parse_px(styled_node->value("font-size"), 16.0f);
            if (control_intrinsic_size(*styled_node->node, control_font > 0 ? control_font : 16.0f, control_w, control_h)) {
                dimensions.content.height = control_h;
            }
        }

        // Handle image intrinsic height and aspect ratio if not set
        if (styled_node->node->tag_name == "img" || styled_node->node->tag_name == "svg") {
            std::string src = resolve_image_src(styled_node->node->data.src());
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
                    int image_w = 0, image_h = 0;
                    if (TextureManager::get().image_size(src, image_w, image_h)) {
                        intrinsic_w = static_cast<float>(image_w);
                        intrinsic_h = static_cast<float>(image_h);
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
                        // the width is already known: the height keeps the proportions of the intrinsic size
                        dimensions.content.height = dimensions.content.width * intrinsic_h / intrinsic_w;
                    }
                } else if (w_str.empty()) {
                    // Maintain aspect ratio based on set height
                    dimensions.content.width = (dimensions.content.height / intrinsic_h) * intrinsic_w;
                }
            }
        }

        // Adjust for inline width/height if not specified
        bool is_abs_fixed = styled_node->value("position") == "absolute" || styled_node->value("position") == "fixed";

        bool is_replaced_element = styled_node->node->tag_name == "img" || styled_node->node->tag_name == "svg" || styled_node->node->tag_name == "video" || is_button_input(*styled_node->node) || forms::kind_of(*styled_node->node) != forms::Kind::None;
        bool should_inline_shrink = (box_type == BoxType::Inline || box_type == BoxType::InlineBlock) && !is_replaced_element;

        // with left and right both set, an absolute box with auto width fills the space they leave (set above)
        bool abs_stretched = is_abs_fixed && !styled_node->value("left").empty() && !styled_node->value("right").empty();
        if (w_str.empty() && !abs_stretched && (dimensions.content.width <= 0.001f || should_inline_shrink || is_abs_fixed))
        {
            if (styled_node->node->type() != NodeType::Text)
            {
                if (!is_replaced_element || dimensions.content.width <= 0.001f)
                {
                    dimensions.content.width = max_x_offset;
                    const std::string min_width = styled_node->value("min-width");
                    if (!min_width.empty() && min_width != "auto") {
                        float font = LayoutEngine::parse_px(styled_node->value("font-size"), 16.0f);
                        float minimum = LayoutEngine::parse_px(min_width, min_width.find('%') != std::string::npos ? container_dimensions.content.width : font);
                        if (styled_node->value("box-sizing") == "border-box")
                            minimum -= dimensions.padding.left + dimensions.padding.right + dimensions.border.left + dimensions.border.right;
                        dimensions.content.width = std::max(dimensions.content.width, std::max(0.0f, minimum));
                    }
                }
            }

            // For pure inline, height should be at least line height if it has content
            if (box_type == BoxType::Inline && dimensions.content.height <= 0.001f && !children.empty())
            {
                dimensions.content.height = line_height;
            }
        }

        // a wrapped text box keeps what its own wrapping found: where its last line ends and how far down it starts
        if (!(children.empty() && text_line_count > 1))
        {
            this->last_line_width = current_x_offset;
            this->total_y_offset = current_y_offset;
        }

        update_total_content_height();

        // Final special cases for html/body
        if (styled_node->node->type() == NodeType::Element)
        {
            auto element = std::static_pointer_cast<ElementNode>(styled_node->node);
            if (g_quirks_mode && element->tag_name == "body" && (styled_node->value("height").empty() || styled_node->value("height") == "auto"))
            {
                auto margin = [](float v) { return v == -12345.0f ? 0.0f : v; };
                float fill = container_dimensions.content.height - margin(dimensions.margin.top) - margin(dimensions.margin.bottom) -
                             dimensions.padding.top - dimensions.padding.bottom - dimensions.border.top - dimensions.border.bottom;
                dimensions.content.height = std::max(dimensions.content.height, fill);
            }
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
