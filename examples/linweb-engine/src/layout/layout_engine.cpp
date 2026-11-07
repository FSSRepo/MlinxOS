#include <cstring>
#include "layout_engine.h"
#include <set>
#include <unordered_map>
#include <functional>
#include <cstdint>
#include "utils/log.h"
#include "renderer/renderer.h"
#include <vector>
#include <memory>
#include <string>

namespace linweb
{
    bool g_quirks_mode = false;

int g_layout_calls = 0;
int g_layout_hits = 0;
uint64_t g_layout_epoch = 0;
}

namespace linweb
{

    /**
     * @brief Parses a string value to pixels, handling units such as %, px, rem and em.
     *
     * @param s The string to parse.
     * @param parent_size The size of the parent container for percentage or em calculations.
     * @return float The value converted to pixels.
     */
    namespace {
        // calc(): sums, differences, products and quotients of lengths, with parentheses. A percentage refers to
        // the reference size the caller passes, as it does for a plain percentage.
        struct CalcEvaluator
        {
            const std::string &s;
            size_t pos;
            float reference;

            void skip() { while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos; }
            bool eat(char c) { skip(); if (pos < s.size() && s[pos] == c) { ++pos; return true; } return false; }

            float expression()
            {
                float v = term();
                for (;;) {
                    if (eat('+')) v += term();
                    else if (eat('-')) v -= term();
                    else return v;
                }
            }

            float term()
            {
                float v = factor();
                for (;;) {
                    if (eat('*')) v *= factor();
                    else if (eat('/')) { float d = factor(); v = d != 0.0f ? v / d : 0.0f; }
                    else return v;
                }
            }

            float factor()
            {
                if (eat('(')) { float v = expression(); eat(')'); return v; }
                skip();
                size_t start = pos;
                if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) ++pos;
                while (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.')) ++pos;
                float number = std::stof(s.substr(start, pos - start));
                if (pos < s.size() && s[pos] == '%') { ++pos; return number / 100.0f * reference; }
                size_t unit_start = pos;
                while (pos < s.size() && std::isalpha(static_cast<unsigned char>(s[pos]))) ++pos;
                std::string unit = s.substr(unit_start, pos - unit_start);
                if (unit == "pt") return number * 96 / 72;
                if (unit == "rem") return number * 16.0f;
                if (unit == "em") return number * reference;
                if (unit == "vh") return number / 100.0f * (float)Renderer::get_viewport_height();
                if (unit == "vw") return number / 100.0f * (float)Renderer::get_viewport_width();
                return number;   // px, or a plain number used as a factor
            }
        };
    }

    float LayoutEngine::parse_px(const std::string &s, float parent_size)
    {
        if (s.empty())
            return 0;
        if (s.rfind("calc(", 0) == 0) {
            try {
                CalcEvaluator evaluator{s, 4, parent_size};
                return evaluator.factor();
            } catch (...) {
                return 0;
            }
        }
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
            size_t pt = s.find("pt");
            if (pt != std::string::npos) return std::stof(s.substr(0, pt)) * 96 / 72;
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

    // Elements the user agent stylesheet does not give a display but that are laid out as boxes of their own: sections of
    // a document, replaced elements and the parts of a table.
    static bool is_block_by_default(const std::string &tag)
    {
        static const std::set<std::string> blocks = {
            "html", "body", "div", "p", "h1", "h2", "h3", "h4", "h5", "h6", "pre", "blockquote", "figure", "figcaption",
            "aside", "address", "fieldset", "legend", "hr", "details", "summary", "hgroup", "menu", "dir", "section",
            "article", "nav", "header", "footer", "main", "form", "ul", "ol", "li", "dl", "dt", "dd", "center",
            "table", "thead", "tbody", "tfoot", "tr", "td", "th", "caption", "colgroup", "col",
            "video", "audio", "canvas", "iframe", "object", "embed", "textarea", "progress", "meter", "select",
            "input", "button", "img", "svg", "option", "optgroup", "datalist", "template", "slot", "script", "style",
            "head", "title", "meta", "link", "base", "noscript", "br", "root", "#text"};
        return blocks.count(tag) > 0 || tag.empty() || tag[0] == '#' || tag[0] == ':';
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
            else if (display.empty() && (styled_node->node->tag_name == "svg" || styled_node->node->tag_name == "img"))
            {
                // replaced elements are inline-level by default: they sit in a line with the text around them
                box_type = BoxType::InlineBlock;
            }
            else if (display.empty() && !is_block_by_default(styled_node->node->tag_name))
            {
                // an unknown or phrasing element (<nextid>, <label>, <abbr>) flows with the text around it
                box_type = BoxType::Inline;
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
    void LayoutBox::layout_children_uncached(Dimensions container_dimensions)
    {
        float current_x_offset = 0;
        float current_y_offset = 0;
        float max_line_height = 0;
        float max_x_offset = 0;

        // Filter children into normal flow and positioned
        std::vector<std::shared_ptr<LayoutBox>> normal_flow_children;
        for (const auto &child : children)
        {
            if ((box_type == BoxType::Flex || box_type == BoxType::Grid) && child->styled_node->node->is_text()) {
                const auto text = std::static_pointer_cast<TextNode>(child->styled_node->node)->data;
                if (text.find_first_not_of(" \t\r\n\f") == std::string::npos) continue;
            }
            if (child->position != PositionType::Absolute && child->position != PositionType::Fixed)
            {
                normal_flow_children.push_back(child);
            }
        }

        if (styled_node->value("display") == "table")
        {
            layout_table(container_dimensions);
        }
        else if (box_type == BoxType::Grid)
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

    // Lays the children out, or restores the result of an earlier call that started from the same container and box state.
    void LayoutBox::layout_children(Dimensions container_dimensions)
    {
        // what the layout of the children works from: the container and the box as it is now
        // the key is the state as it enters: whether the box has been laid out before does not change the result
        laid_out = false;
        const LayoutBoxState in_state = capture_state();
        laid_out = true;
        if (!memo) memo = std::make_shared<std::vector<LayoutMemoEntry>>();
        {
            for (const auto &entry : *memo)
            {
                // the same layout one place over: the key leaves the position out, and a hit moves the result there
                const float dx = in_state.dimensions.content.x - entry.in.dimensions.content.x;
                const float dy = in_state.dimensions.content.y - entry.in.dimensions.content.y;
                if (!entry.movable && (dx != 0.0f || dy != 0.0f)) continue;
                if (container_dimensions.content.x - entry.container.content.x != dx || container_dimensions.content.y - entry.container.content.y != dy) continue;
                Dimensions moved_container = entry.container;
                moved_container.content.x = container_dimensions.content.x;
                moved_container.content.y = container_dimensions.content.y;
                LayoutBoxState moved_in = entry.in;
                moved_in.dimensions.content.x = in_state.dimensions.content.x;
                moved_in.dimensions.content.y = in_state.dimensions.content.y;
                if (std::memcmp(&moved_container, &container_dimensions, sizeof(Dimensions)) == 0 &&
                    std::memcmp(&moved_in, &in_state, sizeof(LayoutBoxState)) == 0)
                {
                    ++g_layout_hits;
                    restore_state(entry.out);
                    dimensions.content.x += dx;
                    dimensions.content.y += dy;
                    size_t next = 0;
                    restore_descendants(entry.descendants, next, dx, dy);
                    return;
                }
            }
        }

        layout_children_uncached(container_dimensions);
        // flex and grid place their items after the height was first worked out
        if (styled_node->value("display") != "table") update_total_content_height();

        {
            if (memo->size() >= 8) memo->erase(memo->begin());
            LayoutMemoEntry entry;
            entry.container = container_dimensions;
            entry.in = in_state;
            entry.out = capture_state();
            capture_descendants(entry.descendants);
            entry.movable = !has_out_of_flow_descendant();
            memo->push_back(std::move(entry));
        }
    }

    LayoutBoxState LayoutBox::capture_state() const
    {
        LayoutBoxState state;
        std::memset(&state, 0, sizeof(state));
        state.dimensions = dimensions;
        state.last_line_width = last_line_width;
        state.text_leading_trim = text_leading_trim;
        state.total_y_offset = total_y_offset;
        state.scroll_y = scroll_y;
        state.containing_block_width = containing_block_width;
        state.containing_block_height = containing_block_height;
        state.total_content_height = total_content_height;
        state.line_height = line_height;
        state.text_first_width = text_first_width;
        state.text_line_count = text_line_count;
        state.laid_out = laid_out;
        return state;
    }

    void LayoutBox::restore_state(const LayoutBoxState& state)
    {
        dimensions = state.dimensions;
        last_line_width = state.last_line_width;
        text_leading_trim = state.text_leading_trim;
        total_y_offset = state.total_y_offset;
        scroll_y = state.scroll_y;
        containing_block_width = state.containing_block_width;
        containing_block_height = state.containing_block_height;
        total_content_height = state.total_content_height;
        line_height = state.line_height;
        text_first_width = state.text_first_width;
        text_line_count = state.text_line_count;
        laid_out = state.laid_out;
    }

    void LayoutBox::capture_descendants(std::vector<LayoutBoxState>& states) const
    {
        for (const auto &child : children)
        {
            states.push_back(child->capture_state());
            child->capture_descendants(states);
        }
    }

    void LayoutBox::restore_descendants(const std::vector<LayoutBoxState>& states, size_t& next, float dx, float dy)
    {
        for (const auto &child : children)
        {
            child->restore_state(states[next]);
            if (states[next].laid_out)
            {
                child->dimensions.content.x += dx;
                child->dimensions.content.y += dy;
            }
            ++next;
            child->restore_descendants(states, next, dx, dy);
        }
    }

    bool LayoutBox::has_out_of_flow_descendant() const
    {
        for (const auto &child : children)
        {
            if (child->position == PositionType::Absolute || child->position == PositionType::Fixed || child->has_out_of_flow_descendant())
                return true;
        }
        return false;
    }

    void LayoutBox::layout(Dimensions container_dimensions)
    {
        ++g_layout_calls;
        resolve_dimensions(container_dimensions);
        apply_relative_position(container_dimensions);

        if (shrinks_to_fit(container_dimensions)) {
            // Shrink-to-fit: lay the content out in the space available, which leaves the content's preferred width
            // in the box, then take the smaller of that and the space, and lay the content out again in that width.
            float available = available_width_of(container_dimensions);
            dimensions.content.width = available;
            layout_children(container_dimensions);
            // the preferred width is what the content asks for when nothing wraps; blocks inside only fill the space
            dimensions.content.width = std::min(max_content_width(false), available);
        }
        layout_children(container_dimensions);
        center_button_content();
        layout_positioned_children();

    }

    bool LayoutBox::is_empty_inline() const
    {
        const auto &node = styled_node->node;
        if (node->type() == NodeType::Text)
            return static_cast<TextNode &>(*node).data.find_first_not_of(" \t\n\r\f") == std::string::npos;
        if (node->type() != NodeType::Element || std::dynamic_pointer_cast<PseudoElementNode>(node)) return false;
        const std::string &tag = node->tag_name;
        if (tag == "img" || tag == "svg" || tag == "input" || tag == "select" || tag == "textarea" || tag == "button" ||
            tag == "br" || tag == "video" || tag == "canvas" || tag == "progress")
            return false;
        if (box_type != BoxType::Inline) return false;
        // a padding or border gives it a size of its own
        if (dimensions.padding.left + dimensions.padding.right + dimensions.border.left + dimensions.border.right > 0.0f) return false;
        for (const auto &child : children)
            if (!child->is_empty_inline()) return false;
        return true;
    }

    float LayoutBox::max_content_width(bool with_extras) const
    {
        const auto &node = styled_node->node;
        const bool is_text = node->type() == NodeType::Text || std::dynamic_pointer_cast<PseudoElementNode>(node);
        if (is_text) return measure_text_run(text_to_show());

        auto side = [](float v) { return v == -12345.0f ? 0.0f : v; };
        float extras = side(dimensions.margin.left) + side(dimensions.margin.right) + dimensions.padding.left +
                       dimensions.padding.right + dimensions.border.left + dimensions.border.right;

        // a box with a width of its own, and a replaced element or a control, is as wide as it was laid out
        const std::string width = styled_node->value("width");
        const std::string &tag = node->tag_name;
        const bool replaced = tag == "img" || tag == "svg" || tag == "input" || tag == "textarea" || tag == "select" ||
                              tag == "button" || tag == "video" || tag == "canvas" || tag == "progress";
        float inner = 0;
        if (replaced || (!width.empty() && width != "auto" && width.find('%') == std::string::npos)) {
            inner = dimensions.content.width;
        } else {
            const bool row = box_type == BoxType::Flex && styled_node->value("flex-direction") != "column";
            std::string gap_text = styled_node->value("column-gap");
            if (gap_text.empty()) gap_text = styled_node->value("gap");
            const float gap = gap_text.empty() ? 0.0f : LayoutEngine::parse_px(gap_text, 0.0f);
            float best = 0, run = 0;
            int counted = 0;
            for (const auto &child : children) {
                const std::string position = child->styled_node->value("position");
                if (position == "absolute" || position == "fixed") continue;
                const bool blank = child->styled_node->node->type() == NodeType::Text && child->text_to_show().find_first_not_of(" \t\n\r\f") == std::string::npos;
                if (blank && child->box_type != BoxType::Block) continue;
                const float child_width = child->max_content_width(true);
                const bool inline_level = child->box_type == BoxType::Inline || child->box_type == BoxType::InlineBlock ||
                                          child->styled_node->node->type() == NodeType::Text;
                if (row) {
                    run += child_width + (counted++ > 0 ? gap : 0.0f);
                } else if (inline_level) {
                    run += child_width;
                } else {
                    best = std::max(best, run);
                    run = 0;
                    best = std::max(best, child_width);
                }
            }
            inner = std::max(best, run);
        }
        return with_extras ? inner + extras : inner;
    }

    // An absolutely positioned box with no width of its own (and not stretched between left and right) is sized by its
    // content, as CSS shrink-to-fit does. A replaced element keeps its own intrinsic size.
    bool LayoutBox::shrinks_to_fit(Dimensions container_dimensions) const
    {
        std::string position = styled_node->value("position");
        if (position != "absolute" && position != "fixed") return false;
        std::string width = styled_node->value("width");
        if (!width.empty() && width != "auto") return false;
        if (!styled_node->value("left").empty() && !styled_node->value("right").empty()) return false;
        const std::string& tag = styled_node->node->tag_name;
        return tag != "img" && tag != "svg" && tag != "input" && tag != "textarea" && tag != "select" && tag != "button";
    }

    // The width an absolutely positioned box can take: its containing block less the sides it is placed from.
    float LayoutBox::available_width_of(Dimensions container_dimensions) const
    {
        float cb = containing_block_width > 0 ? containing_block_width : container_dimensions.content.width;
        float space = cb;
        std::string left = styled_node->value("left");
        std::string right = styled_node->value("right");
        if (!left.empty()) space -= LayoutEngine::parse_px(left, cb);
        if (!right.empty()) space -= LayoutEngine::parse_px(right, cb);
        float ml = dimensions.margin.left == -12345.0f ? 0 : dimensions.margin.left;
        float mr = dimensions.margin.right == -12345.0f ? 0 : dimensions.margin.right;
        space -= ml + mr + dimensions.padding.left + dimensions.padding.right +
                 dimensions.border.left + dimensions.border.right;
        return std::max(0.0f, space);
    }

    namespace
    {
        // The layout of a subtree is kept from frame to frame under a fingerprint of everything that decides it: the
        // style, the attributes and text of its nodes, the kind of each box, and the size of the viewport. A frame that
        // moves only a few animated boxes then finds the result of every other subtree and does not lay it out again.
        struct MemoSlot
        {
            std::shared_ptr<std::vector<LayoutBox::LayoutMemoEntry>> entries;
            uint64_t used = 0;
        };
        std::unordered_map<uint64_t, MemoSlot> g_memo_cache;
        uint64_t g_memo_frame = 0;
        int g_build_depth = 0;

        inline uint64_t mix(uint64_t h, uint64_t v)
        {
            h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            return h;
        }

        uint64_t hash_text(const std::string &s)
        {
            return std::hash<std::string>()(s);
        }

        // order does not matter: the sum of the hashes of the entries
        template <typename Map>
        uint64_t hash_map(const Map &map)
        {
            uint64_t sum = 0;
            for (const auto &entry : map) sum += mix(hash_text(entry.first), hash_text(entry.second));
            return sum;
        }

        uint64_t fingerprint(const LayoutBox &box)
        {
            const Node &node = *box.styled_node->node;
            uint64_t h = 1469598103934665603ULL;
            h = mix(h, static_cast<uint64_t>(Renderer::get_viewport_width()));
            h = mix(h, static_cast<uint64_t>(Renderer::get_viewport_height()));
            h = mix(h, g_quirks_mode ? 1 : 0);
            h = mix(h, g_layout_epoch);
            h = mix(h, static_cast<uint64_t>(box.box_type));
            h = mix(h, static_cast<uint64_t>(box.position));
            h = mix(h, static_cast<uint64_t>(box.z_index));
            h = mix(h, hash_text(node.tag_name));
            if (auto pseudo = dynamic_cast<const PseudoElementNode *>(&node))
                h = mix(h, hash_text(pseudo->pseudo_content));   // a pseudo-element is made again every frame: no identity
            else
                h = mix(h, node.uid);
            if (node.type() == NodeType::Text)
                h = mix(h, hash_text(static_cast<const TextNode &>(node).data));
            h = mix(h, hash_map(node.data.attributes));
            h = mix(h, hash_map(box.styled_node->specified_values));
            h = mix(h, hash_map(box.styled_node->animated_values));
            for (const auto &child : box.children) h = mix(h, child->memo_fingerprint);
            return h;
        }

        // children first: a box's fingerprint contains theirs
        void assign_memo(LayoutBox &box)
        {
            for (const auto &child : box.children) assign_memo(*child);
            box.memo_fingerprint = fingerprint(box);
            MemoSlot &slot = g_memo_cache[box.memo_fingerprint];
            if (!slot.entries) slot.entries = std::make_shared<std::vector<LayoutBox::LayoutMemoEntry>>();
            slot.used = g_memo_frame;
            box.memo = slot.entries;
        }
    }

    std::shared_ptr<LayoutBox> LayoutEngine::build_layout_tree(const std::shared_ptr<StyledNode> &styled_node)
    {
        struct DepthGuard
        {
            DepthGuard() { ++g_build_depth; }
            ~DepthGuard() { --g_build_depth; }
        } guard;
        auto result = build_layout_tree_boxes(styled_node);
        if (g_build_depth == 1)
        {
            // the tree is complete (a flex item is blockified by its parent after the item was built)
            ++g_memo_frame;
            if (result) assign_memo(*result);
            for (auto it = g_memo_cache.begin(); it != g_memo_cache.end();)
                it = (it->second.used == g_memo_frame) ? std::next(it) : g_memo_cache.erase(it);
        }
        return result;
    }

    std::shared_ptr<LayoutBox> LayoutEngine::build_layout_tree_boxes(const std::shared_ptr<StyledNode> &styled_node)
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
            auto layout_child = build_layout_tree_boxes(child);
            if (layout_child)
            {
                layout_child->parent = root;
                root->children.push_back(layout_child);
            }
        }

        // An item of a flex container is blockified (CSS). An inline one that grows to fill the free space needs a
        // width of its own to keep, which an inline box never has: it is made a block. Items sized by their content
        // stay as they are.
        if (root->box_type == BoxType::Flex) {
            auto grows = [](const std::shared_ptr<StyledNode>& item) {
                try {
                    std::string grow = item->value("flex-grow");
                    if (!grow.empty()) return std::stof(grow) > 0.0f;
                    std::string shorthand = item->value("flex");
                    if (shorthand.empty()) return false;
                    std::string first = shorthand.substr(0, shorthand.find(' '));
                    return first == "auto" || std::stof(first) > 0.0f;
                } catch (...) {
                    return false;
                }
            };
            for (const auto& child : root->children) {
                if (child->styled_node->node->type() == NodeType::Element && !std::dynamic_pointer_cast<PseudoElementNode>(child->styled_node->node) &&
                    (child->box_type == BoxType::Inline || child->box_type == BoxType::InlineBlock) && grows(child->styled_node))
                    child->box_type = BoxType::Block;
            }
        }

        // An inline wrapper containing only blocks establishes an anonymous
        // block flow, rather than shrinking all descendants onto one line.
        if (root->box_type == BoxType::Inline && !root->children.empty()) {
            bool only_blocks = true;
            for (const auto& child : root->children) {
                if (child->box_type == BoxType::Inline || child->box_type == BoxType::InlineBlock) only_blocks = false;
            }
            if (only_blocks) root->box_type = BoxType::Block;
        }
        return root;
    }

} // namespace linweb
