#include "layout_engine.h"
#include <vector>
#include <memory>
#include <string>
#include <sstream>
#include <algorithm>
#include <tuple>

namespace linweb
{

    namespace {
        // One track size: negative for fr units (-1 for 1fr), pixels otherwise. A minmax(min, max) track takes its max.
        // Never throws: a value that is not a number is treated as 0 (or 1fr for a bare "fr").
        float parse_track(const std::string& part, float container) {
            std::string value = part;
            if (value.compare(0, 7, "minmax(") == 0) {
                size_t comma = value.find(',');
                value = value.substr(comma == std::string::npos ? 7 : comma + 1);
                if (!value.empty() && value.back() == ')') value.pop_back();
            }
            if (value.size() >= 2 && value.compare(value.size() - 2, 2, "fr") == 0) {
                try { return -std::stof(value.substr(0, value.size() - 2)); } catch (...) { return -1.0f; }
            }
            return LayoutEngine::parse_px(value, container);
        }
    }

    /**
     * @brief Resolves the column widths for the grid layout (Grid).
     *
     * @param container_width Container width.
     * @param col_widths Vector that will be filled with the computed column widths.
     * @param total_width Reference to store the computed total width.
     */
    void LayoutBox::grid_resolve_columns(float container_width, std::vector<float> &col_widths, float &total_width)
    {
        std::string columns_str = styled_node->value("grid-template-columns");
        std::string gap_str = styled_node->value("column-gap");
        if (gap_str.empty()) gap_str = styled_node->value("grid-column-gap");
        if (gap_str.empty()) gap_str = styled_node->value("gap");
        if (gap_str.empty()) gap_str = styled_node->value("grid-gap");
        float gap = LayoutEngine::parse_px(gap_str, container_width);

        if (columns_str.empty())
        {
            col_widths.push_back(container_width);
        }
        else
        {
            std::stringstream ss(columns_str);
            std::string part;
            while (ss >> part)
            {
                col_widths.push_back(parse_track(part, container_width));
            }
        }

        float total_fixed = 0;
        float total_fr = 0;
        for (float w : col_widths)
        {
            if (w >= 0)
                total_fixed += w;
            else
                total_fr += -w;
        }
        total_fixed += (col_widths.size() - 1) * gap;

        float remaining = std::max(0.0f, container_width - total_fixed);
        for (float &w : col_widths)
        {
            if (w < 0)
            {
                float fr_ratio = (-w) / total_fr;
                w = remaining * fr_ratio;
            }
        }
        total_width = container_width;
    }

    void LayoutBox::grid_resolve_rows(float container_height, std::vector<float> &row_heights, float &total_height)
    {
        std::string rows_str = styled_node->value("grid-template-rows");
        std::string gap_str = styled_node->value("row-gap");
        if (gap_str.empty()) gap_str = styled_node->value("grid-row-gap");
        if (gap_str.empty()) gap_str = styled_node->value("gap");
        if (gap_str.empty()) gap_str = styled_node->value("grid-gap");
        float gap = LayoutEngine::parse_px(gap_str, container_height);

        if (!rows_str.empty())
        {
            std::stringstream ss(rows_str);
            std::string part;
            while (ss >> part)
            {
                row_heights.push_back(parse_track(part, container_height));
            }
        }

        float total_fixed = 0;
        float total_fr = 0;
        for (float h : row_heights)
        {
            if (h >= 0)
                total_fixed += h;
            else
                total_fr += -h;
        }
        if (!row_heights.empty()) total_fixed += (row_heights.size() - 1) * gap;

        float remaining = std::max(0.0f, container_height - total_fixed);
        for (float &h : row_heights)
        {
            if (h < 0)
            {
                float fr_ratio = (-h) / total_fr;
                h = remaining * fr_ratio;
            }
        }
        total_height = container_height;
    }

    /**
     * @brief Positions the elements inside the grid (Grid).
     *
     * @param children Children in the normal flow to position.
     * @param col_widths Resolved column widths.
     * @param total_height Reference to store the total height of the grid.
     * @param max_width Reference to store the maximum width.
     */
    void LayoutBox::grid_layout_items(Dimensions container_dimensions, const std::vector<std::shared_ptr<LayoutBox>> &children, const std::vector<float> &col_widths, const std::vector<float> &explicit_row_heights, float &total_height, float &max_width)
    {
        std::string col_gap_str = styled_node->value("column-gap");
        if (col_gap_str.empty()) col_gap_str = styled_node->value("grid-column-gap");
        if (col_gap_str.empty()) col_gap_str = styled_node->value("gap");
        if (col_gap_str.empty()) col_gap_str = styled_node->value("grid-gap");
        float column_gap = LayoutEngine::parse_px(col_gap_str, dimensions.content.width);

        std::string row_gap_str = styled_node->value("row-gap");
        if (row_gap_str.empty()) row_gap_str = styled_node->value("grid-row-gap");
        if (row_gap_str.empty()) row_gap_str = styled_node->value("gap");
        if (row_gap_str.empty()) row_gap_str = styled_node->value("grid-gap");
        float row_gap = LayoutEngine::parse_px(row_gap_str, dimensions.content.height);

        int num_cols = col_widths.empty() ? 1 : static_cast<int>(col_widths.size());

        auto parse_grid_line = [](const std::string& s) -> int {
            if (s.empty() || s == "auto") return -1;
            try { return std::stoi(s); } catch (...) { return -1; }
        };

        auto parse_placement = [&](const std::shared_ptr<LayoutBox>& child, int& c_start, int& c_end, int& r_start, int& r_end) {
            c_start = c_end = r_start = r_end = -1;
            std::string area = child->styled_node->value("grid-area");
            if (!area.empty()) {
                // "row-start / col-start / row-end / col-end": a slash separates the lines like a space does
                std::replace(area.begin(), area.end(), '/', ' ');
                std::stringstream ss(area);
                std::string rs, cs, re, ce;
                ss >> rs >> cs >> re >> ce;
                r_start = parse_grid_line(rs);
                c_start = parse_grid_line(cs);
                r_end = parse_grid_line(re);
                c_end = parse_grid_line(ce);
                return;
            }
            std::string col = child->styled_node->value("grid-column");
            if (!col.empty()) {
                size_t slash = col.find('/');
                if (slash != std::string::npos) {
                    c_start = parse_grid_line(col.substr(0, slash));
                    c_end = parse_grid_line(col.substr(slash + 1));
                } else {
                    c_start = parse_grid_line(col);
                }
            }
            std::string row = child->styled_node->value("grid-row");
            if (!row.empty()) {
                size_t slash = row.find('/');
                if (slash != std::string::npos) {
                    r_start = parse_grid_line(row.substr(0, slash));
                    r_end = parse_grid_line(row.substr(slash + 1));
                } else {
                    r_start = parse_grid_line(row);
                }
            }
        };

        struct Placement { int c_start, c_end, r_start, r_end; };
        std::vector<Placement> placements;
        placements.reserve(children.size());

        std::vector<std::tuple<int,int,int,int>> occupied;

        int auto_row = 1, auto_col = 1;
        int max_explicit_row = 1;

        for (const auto& child : children) {
            int c_start, c_end, r_start, r_end;
            parse_placement(child, c_start, c_end, r_start, r_end);

            if (c_start > 0 && c_end <= 0) c_end = c_start + 1;
            if (r_start > 0 && r_end <= 0) r_end = r_start + 1;

            if (c_start <= 0 || r_start <= 0) {
                // auto placement (simple dense flow)
                bool placed = false;
                for (int r = auto_row; !placed; ++r) {
                    for (int c = (r == auto_row ? auto_col : 1); c <= num_cols && !placed; ++c) {
                        bool overlap = false;
                        for (const auto& occ : occupied) {
                            int os, oe, rs, re;
                            std::tie(os, oe, rs, re) = occ;
                            if (c < oe && c + 1 > os && r < re && r + 1 > rs) {
                                overlap = true; break;
                            }
                        }
                        if (!overlap) {
                            c_start = c; c_end = c + 1;
                            r_start = r; r_end = r + 1;
                            placed = true;
                            auto_row = r;
                            auto_col = c + 1;
                            if (auto_col > num_cols) { auto_col = 1; auto_row++; }
                        }
                    }
                }
            }
            c_start = std::max(1, c_start);
            r_start = std::max(1, r_start);
            if (c_end <= c_start) c_end = c_start + 1;
            if (r_end <= r_start) r_end = r_start + 1;
            if (c_end > num_cols + 1) c_end = num_cols + 1;

            placements.push_back({c_start, c_end, r_start, r_end});
            occupied.emplace_back(c_start, c_end, r_start, r_end);
            max_explicit_row = std::max(max_explicit_row, r_end - 1);
        }

        // First pass: layout children to measure natural heights
        std::vector<float> child_natural_heights(children.size(), 0.0f);
        for (size_t i = 0; i < children.size(); ++i) {
            const auto& child = children[i];
            const auto& p = placements[i];
            float avail_width = 0;
            for (int c = p.c_start; c < p.c_end; ++c)
                avail_width += col_widths[c - 1];
            avail_width += column_gap * (p.c_end - p.c_start - 1);

            Dimensions child_container;
            child_container.content.width = std::max(0.0f, avail_width);
            child_container.content.height = container_dimensions.content.height;
            child->layout(child_container);

            child_natural_heights[i] = child->dimensions.content.height +
                child->dimensions.padding.top + child->dimensions.padding.bottom +
                child->dimensions.border.top + child->dimensions.border.bottom +
                child->dimensions.margin.top + child->dimensions.margin.bottom;
        }

        // Resolve row heights (explicit + implicit)
        std::vector<float> row_heights = explicit_row_heights;
        if (row_heights.size() < static_cast<size_t>(max_explicit_row)) {
            row_heights.resize(max_explicit_row, 0.0f);
        }

        for (size_t i = 0; i < children.size(); ++i) {
            const auto& p = placements[i];
            for (int r = p.r_start; r < p.r_end && r > 0; ++r) {
                if (static_cast<size_t>(r) > row_heights.size()) row_heights.resize(r, 0.0f);
                row_heights[r - 1] = std::max(row_heights[r - 1], child_natural_heights[i]);
            }
        }

        int num_rows = static_cast<int>(row_heights.size());

        // Compute column x offsets
        std::vector<float> col_x(num_cols + 1, 0.0f);
        for (int c = 2; c <= num_cols; ++c)
            col_x[c] = col_x[c - 1] + col_widths[c - 2] + column_gap;

        // Compute row y offsets
        std::vector<float> row_y(num_rows + 1, 0.0f);
        for (int r = 2; r <= num_rows; ++r)
            row_y[r] = row_y[r - 1] + row_heights[r - 2] + row_gap;

        // Second pass: position children
        for (size_t i = 0; i < children.size(); ++i) {
            const auto& child = children[i];
            const auto& p = placements[i];
            float x = dimensions.content.x + col_x[p.c_start];
            float y = dimensions.content.y + row_y[p.r_start];
            float w = 0;
            for (int c = p.c_start; c < p.c_end; ++c)
                w += col_widths[c - 1];
            w += column_gap * (p.c_end - p.c_start - 1);
            // justify-items / justify-self: center gives an item without a width its content's width, centered in its cell
            std::string justify = child->styled_node->value("justify-self");
            if (justify.empty() || justify == "auto") justify = styled_node->value("justify-items");
            std::string own_width = child->styled_node->value("width");
            if (justify == "center" && (own_width.empty() || own_width == "auto")) {
                float content_w = 0;
                for (const auto& grandchild : child->children) {
                    if (grandchild->position == PositionType::Absolute || grandchild->position == PositionType::Fixed) continue;
                    float outer = grandchild->dimensions.content.width + grandchild->dimensions.padding.left + grandchild->dimensions.padding.right +
                                  grandchild->dimensions.border.left + grandchild->dimensions.border.right;
                    content_w += outer;   // inline-level items sit side by side in the item's line
                }
                if (content_w > 0 && content_w < w) {
                    x += (w - content_w) / 2.0f;
                    w = content_w;
                }
            }
            float h = child_natural_heights[i];
            if (p.r_end > p.r_start && p.r_end - 1 <= num_rows) {
                h = 0;
                for (int r = p.r_start; r < p.r_end; ++r) h += row_heights[r - 1];
                h += row_gap * (p.r_end - p.r_start - 1);
            }

            // vertical placement inside the cell: margin-top:auto pushes the item to the bottom, align-items:center
            // centers it (an item's own size is what is left once its margins are taken out)
            float item_height = child->dimensions.content.height;
            float cell_height = 0;
            for (int r = p.r_start; r < p.r_end && r > 0 && static_cast<size_t>(r) <= row_heights.size(); ++r)
                cell_height += row_heights[r - 1];
            cell_height = std::min(cell_height, dimensions.content.height);   // a row cannot be taller than its grid
            std::string own_align = child->styled_node->value("align-self");
            if (own_align.empty() || own_align == "auto") own_align = styled_node->value("align-items");
            if (child->styled_node->value("margin-top") == "auto") y += std::max(0.0f, cell_height - item_height);
            else if (own_align == "center") y += std::max(0.0f, (cell_height - item_height) / 2.0f);

            child->dimensions.content.x = x;
            child->dimensions.content.y = y;
            child->dimensions.content.width = std::max(0.0f, w);
            child->dimensions.content.height = std::max(0.0f, h);

            // the item moved, so its children are placed again from its new position (its size is the cell's)
            Dimensions item_container;
            item_container.content.x = x - child->dimensions.margin.left - child->dimensions.padding.left - child->dimensions.border.left;
            item_container.content.y = y - child->dimensions.margin.top - child->dimensions.padding.top - child->dimensions.border.top;
            item_container.content.width = child->dimensions.content.width;
            item_container.content.height = child->dimensions.content.height;
            child->layout_children(item_container);
            child->center_button_content();
        }

        total_height = 0;
        for (float h : row_heights) total_height += h;
        if (num_rows > 1) total_height += row_gap * (num_rows - 1);
        max_width = dimensions.content.width;
    }

    /**
     * @brief Performs the grid layout (Grid).
     *
     * @param container_dimensions Container dimensions.
     * @param normal_flow_children Children in the normal flow.
     */
    void LayoutBox::layout_grid(Dimensions container_dimensions, const std::vector<std::shared_ptr<LayoutBox>> &normal_flow_children)
    {
        std::vector<float> col_widths;
        float total_width = 0;
        grid_resolve_columns(dimensions.content.width, col_widths, total_width);

        std::vector<float> row_heights;
        float total_height_hint = 0;
        grid_resolve_rows(dimensions.content.height, row_heights, total_height_hint);

        float total_height = 0;
        float max_width = 0;
        grid_layout_items(container_dimensions, normal_flow_children, col_widths, row_heights, total_height, max_width);

        resolve_height(container_dimensions, total_height, 0, max_width, 0);
    }

} // namespace linweb
