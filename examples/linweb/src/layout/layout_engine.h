#pragma once
#include "style/style_engine.h"

namespace linweb {

struct Rect {
    float x = 0, y = 0, width = 0, height = 0;
};

struct EdgeSizes {
    float left = 0, right = 0, top = 0, bottom = 0;
};

struct EdgeColors {
    float r[4], g[4], b[4], a[4]; // 0: top, 1: right, 2: bottom, 3: left
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

struct LayoutBox {
    Dimensions dimensions;
    BoxType box_type;
    PositionType position;
    std::shared_ptr<StyledNode> styled_node;
    std::vector<std::shared_ptr<LayoutBox>> children;
    std::weak_ptr<LayoutBox> parent;
    float last_line_width = 0;
    float total_y_offset = 0;
    float scroll_y = 0;
    float total_content_height = 0;
    float line_height = 0;
    int z_index = 0;

    explicit LayoutBox(std::shared_ptr<StyledNode> node);
    
    void layout(Dimensions container_dimensions);

    // Box resolver methods
    void resolve_dimensions(Dimensions container_dimensions);
    void resolve_margin_padding_border(float parent_width, float parent_height, float font_size);
    void resolve_content_width(Dimensions container_dimensions, float font_size);
    void resolve_text_node_dimensions(float font_size, float line_height);
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
    void apply_relative_position(Dimensions container_dimensions);

    // Internal helpers
    void layout_children(Dimensions container_dimensions);
};

class LayoutEngine {
public:
    static std::shared_ptr<LayoutBox> build_layout_tree(const std::shared_ptr<StyledNode>& styled_node);
    static float parse_px(const std::string& s, float parent_size = 0.0f);
};

} // namespace linweb
