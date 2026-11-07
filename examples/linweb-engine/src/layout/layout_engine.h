#pragma once
#include "style/style_engine.h"

namespace linweb {

// Calls to LayoutBox::layout since the counters were last cleared, and how many of them reused an earlier result.
extern int g_layout_calls;
extern int g_layout_hits;
// Bumped when something outside the page changes how its boxes are sized (the picture of a video); the layout kept from
// earlier frames is then dropped.
extern uint64_t g_layout_epoch;

struct Rect {
    float x = 0, y = 0, width = 0, height = 0;
};

struct EdgeSizes {
    float left = 0, right = 0, top = 0, bottom = 0;
};

// Zeroed: the layout memo compares Dimensions byte by byte, so a Dimensions made on the stack must not carry whatever was
// there before (it never matches the one it is compared with, and the box is laid out again).
struct EdgeColors {
    float r[4] = {}, g[4] = {}, b[4] = {}, a[4] = {}; // 0: top, 1: right, 2: bottom, 3: left
};

struct Dimensions {
    Rect content;
    EdgeSizes padding;
    EdgeSizes border;
    EdgeSizes margin;
    EdgeColors border_color;
};

enum class BoxType {
    Block,
    Inline,
    InlineBlock,
    Grid,
    Flex,
    Anonymous
};

enum class PositionType {
    Static,
    Relative,
    Absolute,
    Fixed
};

// Everything layout() reads back from a box and writes into it, apart from its descendants.
struct LayoutBoxState {
    Dimensions dimensions;
    float last_line_width, text_leading_trim, total_y_offset, scroll_y;
    float containing_block_width, containing_block_height, total_content_height, line_height;
    float text_first_width;
    int text_line_count;
    // the box has been through layout(): a box that never was (a skipped white space) is not moved with its subtree.
    // An int, not a bool: the memo compares the state byte by byte and a struct with padding differs in the padding.
    int laid_out;
};
static_assert(sizeof(LayoutBoxState) % alignof(LayoutBoxState) == 0 &&
              sizeof(LayoutBoxState) == sizeof(Dimensions) + 9 * sizeof(float) + 2 * sizeof(int),
              "LayoutBoxState must have no padding: the layout memo compares it with memcmp");

// A page without a doctype is in quirks mode: the user agent margins of the first element of a block are dropped and
// the body fills the window, as in a browser.
extern bool g_quirks_mode;

struct LayoutBox {
    Dimensions dimensions;
    BoxType box_type;
    PositionType position;
    std::shared_ptr<StyledNode> styled_node;
    std::vector<std::shared_ptr<LayoutBox>> children;
    std::weak_ptr<LayoutBox> parent;
    float last_line_width = 0;
    // Width of the leading white space a text box drops at the start of its block; drawing shifts left by it.
    float text_leading_trim = 0;
    float total_y_offset = 0;
    float scroll_y = 0;
    // width of the containing block of an absolutely positioned box, kept across its two layouts (shrink-to-fit uses it)
    float containing_block_width = 0;
    float containing_block_height = 0;
    float total_content_height = 0;
    float line_height = 0;
    // Text that does not fit its line wraps: the width left on the first line when the box was laid out, and the number of
    // lines it takes (1 when it does not wrap). The lines themselves are found again by text_lines().
    float text_first_width = 0;
    int text_line_count = 1;
    bool laid_out = false;
    // the top margin of this box comes from the user agent stylesheet (a heading or a paragraph without a margin of its own)
    bool ua_margin_top = false;
    int z_index = 0;
    uint64_t memo_fingerprint = 0;

    // Results of earlier layout() calls. Flex and grid lay each item out several times (measure, grow, stretch, final) and
    // every call descends the whole subtree, so nested containers multiply. The same container and the same box state
    // after resolve_dimensions() lay the subtree out the same way: the whole result, descendants included, is kept.
    struct LayoutMemoEntry {
        Dimensions container;
        LayoutBoxState in, out;
        std::vector<LayoutBoxState> descendants;
        // false when a descendant is absolutely positioned or fixed: it is placed from a box that may be outside the subtree,
        // so the subtree cannot simply be moved
        bool movable = true;
    };
    // Shared with the boxes of the following frames that have the same fingerprint (see LayoutEngine::build_layout_tree):
    // a subtree that did not change is not laid out again, its result is restored.
    std::shared_ptr<std::vector<LayoutMemoEntry>> memo;
    void capture_descendants(std::vector<LayoutBoxState>& states) const;
    void restore_descendants(const std::vector<LayoutBoxState>& states, size_t& next, float dx, float dy);
    bool has_out_of_flow_descendant() const;
    LayoutBoxState capture_state() const;
    void restore_state(const LayoutBoxState& state);

    explicit LayoutBox(std::shared_ptr<StyledNode> node);
    
    void layout(Dimensions container_dimensions);
    bool shrinks_to_fit(Dimensions container_dimensions) const;
    // The width the box takes when nothing makes its text wrap (its max-content size); margins, padding and border are in it
    // only when asked for.
    float max_content_width(bool with_extras) const;
    // An inline element with no text and nothing replaced inside: it takes no space and makes no line
    bool is_empty_inline() const;
    float available_width_of(Dimensions container_dimensions) const;

    // Box resolver methods
    void resolve_dimensions(Dimensions container_dimensions);
    void resolve_margin_padding_border(float parent_width, float parent_height, float font_size);
    void resolve_content_width(Dimensions container_dimensions, float font_size);
    void resolve_text_node_dimensions(Dimensions container_dimensions, float font_size, float line_height);
    // the text of a text box as it is shown (case transform, white space dropped at the edges of its block)
    std::string text_to_show(float* leading_trim = nullptr) const;
    float measure_text_run(const std::string& text) const;
    std::vector<std::string> text_lines() const;
    float text_block_left() const;
    float text_line_x(size_t index, float line_width) const;
    void resolve_height(Dimensions container_dimensions, float current_y_offset, float max_line_height, float max_x_offset, float current_x_offset);

    // Block layout methods
    void layout_block(Dimensions container_dimensions, const std::vector<std::shared_ptr<LayoutBox>>& normal_flow_children);
    void layout_block_child(Dimensions container_dimensions, const std::shared_ptr<LayoutBox>& child, float& current_x_offset, float& current_y_offset, float& max_line_height, float& max_x_offset, std::vector<std::shared_ptr<LayoutBox>>& current_line, bool& has_prev_block, float& pending_prev_block_bottom_margin, bool& has_any_flow_content, bool parent_can_collapse_top, bool parent_can_collapse_bottom);
    void apply_line_alignment(const std::vector<std::shared_ptr<LayoutBox>>& line, float line_width);
    float collapse_margins(float a, float b);

    // Inline layout methods
    void layout_inline_child(const std::shared_ptr<LayoutBox>& child, Dimensions container_dimensions, float& current_x_offset, float& current_y_offset, float& max_line_height, float& max_x_offset, std::vector<std::shared_ptr<LayoutBox>>& current_line, float line_height);

    // Flex layout methods
    void layout_flex(Dimensions container_dimensions, const std::vector<std::shared_ptr<LayoutBox>>& normal_flow_children);
    void layout_flex_items(Dimensions container_dimensions, const std::vector<std::shared_ptr<LayoutBox>>& normal_flow_children);
    void flex_initial_pass(Dimensions container_dimensions, const std::vector<std::shared_ptr<LayoutBox>>& children, bool is_column, float& main_offset, float& cross_size);
    void flex_distribute_grow(Dimensions container_dimensions, const std::vector<std::shared_ptr<LayoutBox>>& children, bool is_column, float& main_offset);
    void flex_justify_content(const std::vector<std::shared_ptr<LayoutBox>>& children, bool is_column, float main_offset, float& start_main_offset, float& main_gap);
    void flex_align_and_position(const std::vector<std::shared_ptr<LayoutBox>>& children, bool is_column, float start_main_offset, float main_gap);

    // Grid layout methods
    void layout_grid(Dimensions container_dimensions, const std::vector<std::shared_ptr<LayoutBox>>& normal_flow_children);
    void grid_resolve_columns(float container_width, std::vector<float>& col_widths, float& total_width);
    void grid_resolve_rows(float container_height, std::vector<float>& row_heights, float& total_height);
    void grid_layout_items(Dimensions container_dimensions, const std::vector<std::shared_ptr<LayoutBox>>& children, const std::vector<float>& col_widths, const std::vector<float>& row_heights, float& total_height, float& max_width);

    // Positioned layout methods
    void layout_positioned_children();
    void center_button_content();
    void apply_relative_position(Dimensions container_dimensions);

    // Internal helpers
    void layout_children(Dimensions container_dimensions);
    void layout_children_uncached(Dimensions container_dimensions);
    // The scrollable height: the box and everything its in-flow children cover. Needs the children in their final places.
    void update_total_content_height();
    void layout_table(Dimensions container_dimensions);
};

class LayoutEngine {
public:
    static std::shared_ptr<LayoutBox> build_layout_tree(const std::shared_ptr<StyledNode>& styled_node);
    // The boxes of the tree, without the layout kept from earlier frames (build_layout_tree adds it)
    static std::shared_ptr<LayoutBox> build_layout_tree_boxes(const std::shared_ptr<StyledNode>& styled_node);
    static float parse_px(const std::string& s, float parent_size = 0.0f);
};

} // namespace linweb
