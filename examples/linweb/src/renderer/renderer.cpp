#include "renderer/renderer.h"
#include "renderer/renderer_internal.h"
#include "renderer/matrix_math.h"
#include "renderer/texture_manager.h"
#include "renderer/svg_loader.h"
#include "renderer/scrollbar.h"
#include "style/colors.h"
#include <algorithm>
#include <sstream>

namespace linweb {

    void parse_color(const std::string &value, float &r, float &g, float &b, float &a)
    {
        Color c = parse_color_str(value);
        r = c.r;
        g = c.g;
        b = c.b;
        a = c.a;
    }

    std::string extract_color_part(const std::string &part)
    {
        size_t space_pos = part.find(' ');
        if (space_pos != std::string::npos)
            return trim(part.substr(0, space_pos));
        return part;
    }

    std::pair<std::string, std::string> split_color_stop(const std::string& part)
    {
        int paren_depth = 0;
        size_t i = 0;
        for (; i < part.size(); ++i) {
            if (part[i] == '(') paren_depth++;
            else if (part[i] == ')') paren_depth--;
            else if (std::isspace(static_cast<unsigned char>(part[i])) && paren_depth == 0) {
                break;
            }
        }
        return {trim(part.substr(0, i)), trim(part.substr(i))};
    }

    float parse_stop_value(const std::string& s)
    {
        if (s.empty()) return -1.0f;
        if (s.back() == '%') {
            try { return std::stof(s.substr(0, s.size() - 1)) / 100.0f; } catch(...) {}
            return -1.0f;
        }
        try { return std::stof(s); } catch(...) { return -1.0f; }
    }

    void distribute_stops(std::vector<float>& stops)
    {
        int first_explicit = -1;
        for (int i = 0; i < (int)stops.size(); ++i) {
            if (stops[i] >= 0) { first_explicit = i; break; }
        }
        int last_explicit = -1;
        for (int i = (int)stops.size() - 1; i >= 0; --i) {
            if (stops[i] >= 0) { last_explicit = i; break; }
        }
        if (first_explicit == -1) {
            for (int i = 0; i < (int)stops.size(); ++i) {
                stops[i] = (float)i / (stops.size() - 1);
            }
            return;
        }
        for (int i = 0; i < first_explicit; ++i) stops[i] = 0.0f;
        for (int i = last_explicit + 1; i < (int)stops.size(); ++i) stops[i] = 1.0f;
        int prev = first_explicit;
        for (int i = first_explicit + 1; i <= last_explicit; ++i) {
            if (stops[i] >= 0) {
                float step = (stops[i] - stops[prev]) / (i - prev);
                for (int j = prev + 1; j < i; ++j) {
                    stops[j] = stops[prev] + step * (j - prev);
                }
                prev = i;
            }
        }
    }

    BoxShadow parse_box_shadow(const std::string &value)
    {
        BoxShadow shadow;
        if (value.empty() || value == "none")
            return shadow;

        std::string processed = value;
        size_t rgba_pos = processed.find("rgba(");
        if (rgba_pos == std::string::npos)
            rgba_pos = processed.find("rgb(");

        if (rgba_pos != std::string::npos)
        {
            size_t end_pos = processed.find(")", rgba_pos);
            if (end_pos != std::string::npos)
            {
                for (size_t i = rgba_pos; i < end_pos; ++i)
                {
                    if (processed[i] == ' ')
                        processed[i] = ',';
                }
            }
        }

        std::stringstream ss(processed);
        std::string part;
        std::vector<std::string> parts;
        while (ss >> part)
        {
            if (part.back() == ',')
                part.pop_back();
            if (!part.empty() && part[0] == ',')
                part.erase(0, 1);
            if (!part.empty())
                parts.push_back(part);
        }

        if (parts.size() < 2)
            return shadow;

        size_t i = 0;
        if (parts[i] == "inset")
        {
            i++;
        }

        std::vector<float> lengths;
        while (i < parts.size() && (std::isdigit(parts[i][0]) || parts[i][0] == '-' || parts[i][0] == '.'))
        {
            lengths.push_back(parse_length(parts[i]));
            i++;
        }

        if (lengths.size() >= 2)
        {
            shadow.offsetX = lengths[0];
            shadow.offsetY = lengths[1];
            if (lengths.size() >= 3)
                shadow.blur = lengths[2];
            if (lengths.size() >= 4)
                shadow.spread = lengths[3];
            shadow.active = true;
        }

        std::string color_str;
        if (i < parts.size())
        {
            color_str = parts[i];
        }
        else if (parts[0] != "inset" && !std::isdigit(parts[0][0]) && parts[0][0] != '-' && parts[0][0] != '.')
        {
            color_str = parts[0];
        }

        if (!color_str.empty())
        {
            parse_color(color_str, shadow.r, shadow.g, shadow.b, shadow.a);
        }
        else
        {
            shadow.a = 1.0f;
        }

        return shadow;
    }

    float parse_length(const std::string &value)
    {
        std::string s = trim(value);
        if (s.empty())
            return 0.0f;
        size_t px_pos = s.find("px");
        if (px_pos != std::string::npos)
        {
            s = s.substr(0, px_pos);
        }
        else
        {
            size_t rem_pos = s.find("rem");
            if (rem_pos != std::string::npos)
            {
                try
                {
                    return std::stof(s.substr(0, rem_pos)) * 16.0f;
                }
                catch (...)
                {
                    return 0.0f;
                }
            }
        }
        try
        {
            return std::stof(s);
        }
        catch (...)
        {
            return 0.0f;
        }
    }

    Filter parse_filter(const std::string &value)
    {
        Filter filter;
        if (value.empty() || value == "none")
            return filter;

        std::string s = value;
        size_t pos = 0;
        while (pos < s.length())
        {
            size_t open_paren = s.find('(', pos);
            if (open_paren == std::string::npos)
                break;

            std::string func_name = trim(s.substr(pos, open_paren - pos));
            size_t close_paren = s.find(')', open_paren);
            if (close_paren == std::string::npos)
                break;

            std::string args = s.substr(open_paren + 1, close_paren - open_paren - 1);

            if (func_name == "blur")
            {
                filter.blur = parse_length(args);
            }
            else if (func_name == "invert")
            {
                try
                {
                    filter.invert = std::stof(args);
                }
                catch (...)
                {
                    filter.invert = 1.0f;
                }
            }
            else if (func_name == "drop-shadow")
            {
                filter.dropShadow = parse_box_shadow(args);
                filter.hasDropShadow = true;
            }

            pos = close_paren + 1;
            while (pos < s.length() && (std::isspace(s[pos]) || s[pos] == ','))
                pos++;
        }
        return filter;
    }

    Gradient parse_gradient(const std::string &value)
    {
        Gradient grad;
        if (value.empty() || value == "none")
            return grad;

        std::string s = trim(value);
        size_t lin_pos = s.find("linear-gradient(");
        if (lin_pos != std::string::npos)
        {
            grad.type = GradientType::Linear;
            size_t start = lin_pos + 16;
            size_t end = s.find(')', start);
            int p_count = 1;
            for (size_t i = start; i < s.length(); ++i) {
                if (s[i] == '(') p_count++;
                else if (s[i] == ')') {
                    p_count--;
                    if (p_count == 0) {
                        end = i;
                        break;
                    }
                }
            }
            
            if (end != std::string::npos && end > start)
            {
                std::string inner = s.substr(start, end - start);
                std::vector<std::string> parts;
                
                size_t pos = 0;
                int paren_count = 0;
                size_t last_pos = 0;
                while (pos < inner.length()) {
                    if (inner[pos] == '(') paren_count++;
                    if (inner[pos] == ')') paren_count--;
                    if (inner[pos] == ',' && paren_count == 0) {
                        parts.push_back(trim(inner.substr(last_pos, pos - last_pos)));
                        last_pos = pos + 1;
                    }
                    pos++;
                }
                parts.push_back(trim(inner.substr(last_pos)));

                if (parts.size() >= 2)
                {
                    int color_start_idx = 0;
                    if (parts[0].find("deg") != std::string::npos)
                    {
                        try { grad.angle = std::stof(parts[0]); } catch(...) {}
                        color_start_idx = 1;
                    }
                    else if (parts[0].find("to ") == 0)
                    {
                        if (parts[0].find("right") != std::string::npos) grad.angle = 0;
                        else if (parts[0].find("bottom") != std::string::npos) grad.angle = 90;
                        else if (parts[0].find("left") != std::string::npos) grad.angle = 180;
                        else if (parts[0].find("top") != std::string::npos) grad.angle = 270;
                        color_start_idx = 1;
                    }

                    int color_count = (int)parts.size() - color_start_idx;
                    if (color_count > 4) color_count = 4;
                    if (color_count >= 2)
                    {
                        std::vector<Color> colors;
                        std::vector<float> stops;
                        for (int i = 0; i < color_count; ++i) {
                            auto [color_str, stop_str] = split_color_stop(parts[color_start_idx + i]);
                            Color c = parse_color_str(color_str);
                            colors.push_back(c);
                            stops.push_back(parse_stop_value(stop_str));
                        }
                        distribute_stops(stops);

                        grad.color_count = color_count;
                        grad.r1 = colors[0].r; grad.g1 = colors[0].g; grad.b1 = colors[0].b; grad.a1 = colors[0].a; grad.s1 = stops[0];
                        grad.r2 = colors[1].r; grad.g2 = colors[1].g; grad.b2 = colors[1].b; grad.a2 = colors[1].a; grad.s2 = stops[1];
                        if (color_count > 2) {
                            grad.r3 = colors[2].r; grad.g3 = colors[2].g; grad.b3 = colors[2].b; grad.a3 = colors[2].a; grad.s3 = stops[2];
                        }
                        if (color_count > 3) {
                            grad.r4 = colors[3].r; grad.g4 = colors[3].g; grad.b4 = colors[3].b; grad.a4 = colors[3].a; grad.s4 = stops[3];
                        }
                    }
                }
            }
        }
        size_t rad_pos = s.find("radial-gradient(");
        if (rad_pos != std::string::npos)
        {
            grad.type = GradientType::Radial;
            size_t start = rad_pos + 16;
            size_t end = s.find(')', start);
            int p_count = 1;
            for (size_t i = start; i < s.length(); ++i) {
                if (s[i] == '(') p_count++;
                else if (s[i] == ')') {
                    p_count--;
                    if (p_count == 0) {
                        end = i;
                        break;
                    }
                }
            }

            if (end != std::string::npos && end > start)
            {
                std::string inner = s.substr(start, end - start);
                size_t pos = 0;
                int paren_count = 0;
                size_t last_pos = 0;
                std::vector<std::string> parts;
                while (pos < inner.length()) {
                    if (inner[pos] == '(') paren_count++;
                    if (inner[pos] == ')') paren_count--;
                    if (inner[pos] == ',' && paren_count == 0) {
                        parts.push_back(trim(inner.substr(last_pos, pos - last_pos)));
                        last_pos = pos + 1;
                    }
                    pos++;
                }
                parts.push_back(trim(inner.substr(last_pos)));

                if (parts.size() >= 2)
                {
                    std::vector<Color> colors;
                    std::vector<float> stops;
                    int color_count = (int)parts.size();
                    if (color_count > 4) color_count = 4;
                    for (int i = 0; i < color_count; ++i) {
                        auto [color_str, stop_str] = split_color_stop(parts[i]);
                        Color c = parse_color_str(color_str);
                        colors.push_back(c);
                        stops.push_back(parse_stop_value(stop_str));
                    }
                    distribute_stops(stops);

                    grad.color_count = color_count;
                    grad.r1 = colors[0].r; grad.g1 = colors[0].g; grad.b1 = colors[0].b; grad.a1 = colors[0].a; grad.s1 = stops[0];
                    grad.r2 = colors[1].r; grad.g2 = colors[1].g; grad.b2 = colors[1].b; grad.a2 = colors[1].a; grad.s2 = stops[1];
                    if (color_count > 2) {
                        grad.r3 = colors[2].r; grad.g3 = colors[2].g; grad.b3 = colors[2].b; grad.a3 = colors[2].a; grad.s3 = stops[2];
                    }
                    if (color_count > 3) {
                        grad.r4 = colors[3].r; grad.g4 = colors[3].g; grad.b4 = colors[3].b; grad.a4 = colors[3].a; grad.s4 = stops[3];
                    }
                }
            }
        }

        return grad;
    }

    void Renderer::render(const std::shared_ptr<LayoutBox> &root)
    {
        if (!root)
            return;
        glDisable(GL_DEPTH_TEST);
        RendererState::current_model = mat4_identity();

        // Canvas background propagation: if html has no background, use body's if present
        float canvas_r = 1.0f, canvas_g = 1.0f, canvas_b = 1.0f, canvas_a = 0.0f;
        bool has_canvas_bg = false;
        if (root->styled_node && root->styled_node->node && root->styled_node->node->type() == NodeType::Element)
        {
            std::string html_bg = root->styled_node->value("background-color");
            if (html_bg.empty())
                html_bg = root->styled_node->value("background");
            bool html_has_bg = !html_bg.empty() && html_bg != "transparent";

            if (!html_has_bg)
            {
                for (const auto &child : root->children)
                {
                    if (child && child->styled_node && child->styled_node->node &&
                        child->styled_node->node->type() == NodeType::Element &&
                        std::static_pointer_cast<ElementNode>(child->styled_node->node)->tag_name == "body")
                    {
                        std::string body_bg = child->styled_node->value("background-color");
                        if (body_bg.empty())
                            body_bg = child->styled_node->value("background");
                        if (!body_bg.empty() && body_bg != "transparent")
                        {
                            Gradient body_grad = parse_gradient(body_bg);
                            if (body_grad.type != GradientType::None)
                            {
                                // Body has a gradient; let render_box draw it.
                                // Clear with a neutral color so areas outside the body
                                // don't show garbage.
                                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                                glClear(GL_COLOR_BUFFER_BIT);
                                has_canvas_bg = true;
                            }
                            else
                            {
                                Color c = parse_color_str(body_bg);
                                canvas_r = c.r; canvas_g = c.g; canvas_b = c.b; canvas_a = c.a;
                                has_canvas_bg = true;
                                // Clear with body's background color
                                glClearColor(canvas_r, canvas_g, canvas_b, canvas_a);
                                glClear(GL_COLOR_BUFFER_BIT);
                            }
                        }
                        break;
                    }
                }
            }
        }

        if (!has_canvas_bg)
        {
            glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        render_box(root, 1.0f, Filter());
    }

    void Renderer::render_box(const std::shared_ptr<LayoutBox> &box, float inherited_opacity, const Filter &inherited_filter)
    {
        if (!box || !box->styled_node || !box->styled_node->node)
            return;

        if (box->styled_node->node->type() == NodeType::Element && box->styled_node->node->tag_name == "br")
            return;

        GLint old_scissor[4];
        GLboolean old_scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
        if (old_scissor_enabled)
            glGetIntegerv(GL_SCISSOR_BOX, old_scissor);

        std::string overflow = box->styled_node->value("overflow");
        
        bool is_root = (box->parent.lock() == nullptr);

        bool needs_clipping = (overflow == "hidden" || overflow == "scroll" || overflow == "auto" || is_root);
        float clip_x = 0, clip_y = 0, clip_w = 0, clip_h = 0;

        if (needs_clipping)
        {
            clip_x = box->dimensions.content.x - box->dimensions.padding.left + RendererState::current_model[12];
            clip_y = box->dimensions.content.y - box->dimensions.padding.top + RendererState::current_model[13];
            clip_w = box->dimensions.content.width + box->dimensions.padding.left + box->dimensions.padding.right;
            clip_h = box->dimensions.content.height + box->dimensions.padding.top + box->dimensions.padding.bottom;

            if (is_root) {
                clip_x = 0;
                clip_y = 0;
                clip_w = (float)RendererState::viewport_width;
                clip_h = (float)RendererState::viewport_height;
            }
        }

        auto old_model = RendererState::current_model;

        std::string opacity_str = box->styled_node->value("opacity");
        float node_opacity = 1.0f;
        bool has_own_opacity = false;

        if (!opacity_str.empty())
        {
            try
            {
                node_opacity = std::stof(opacity_str);
                has_own_opacity = true;
            }
            catch (...)
            {
            }
        }

        float total_opacity = has_own_opacity ? node_opacity : inherited_opacity;

        Filter local_filter = parse_filter(box->styled_node->value("filter"));
        Filter combined_filter;
        combined_filter.blur = local_filter.blur + inherited_filter.blur;

        combined_filter.invert =
            std::min(1.0f, local_filter.invert + inherited_filter.invert);

        combined_filter.dropShadow = local_filter.hasDropShadow ? local_filter.dropShadow : inherited_filter.dropShadow;
        combined_filter.hasDropShadow = local_filter.hasDropShadow || inherited_filter.hasDropShadow;

        if (box->styled_node->transform.has_transform)
        {
            const auto &t = box->styled_node->transform;

            float border_l = box->dimensions.border.left;
            float border_t = box->dimensions.border.top;
            float border_r = box->dimensions.border.right;
            float border_b = box->dimensions.border.bottom;

            float bg_x = box->dimensions.content.x - box->dimensions.padding.left - border_l;
            float bg_y = box->dimensions.content.y - box->dimensions.padding.top - border_t;
            float bg_w = box->dimensions.content.width + box->dimensions.padding.left + box->dimensions.padding.right + border_l + border_r;
            float bg_h = box->dimensions.content.height + box->dimensions.padding.top + box->dimensions.padding.bottom + border_t + border_b;

            float origin_x = bg_x + bg_w * t.origin_x;
            float origin_y = bg_y + bg_h * t.origin_y;

            float tx = t.translate_x;
            float ty = t.translate_y;
            if (t.translate_x_is_percent) tx = (t.translate_x / 100.0f) * bg_w;
            if (t.translate_y_is_percent) ty = (t.translate_y / 100.0f) * bg_h;

            auto m = RendererState::current_model;
            m = mat4_mul(m, mat4_translate(origin_x, origin_y));
            m = mat4_mul(m, mat4_translate(tx, ty));
            m = mat4_mul(m, mat4_rotate(t.rotate));
            m = mat4_mul(m, mat4_scale(t.scale_x, t.scale_y));
            m = mat4_mul(m, mat4_translate(-origin_x, -origin_y));

            RendererState::current_model = m;
        }

        if (box->styled_node->node->type() == NodeType::Element)
        {
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            std::string bg_color_val = box->styled_node->value("background-color");
            std::string bg_val = box->styled_node->value("background");
            
            if (!bg_color_val.empty()) {
                parse_color(bg_color_val, r, g, b, a);
            } else if (!bg_val.empty()) {
                Color c = parse_color_str(bg_val);
                if (c.a > 0.0f || bg_val == "transparent") {
                    r = c.r; g = c.g; b = c.b; a = c.a;
                } else {
                    std::stringstream ss(bg_val);
                    std::string part;
                    while (ss >> part) {
                        if (part.find("gradient") != std::string::npos) continue;
                        Color cp = parse_color_str(part);
                        if (cp.a > 0.0f || part == "transparent") {
                            r = cp.r; g = cp.g; b = cp.b; a = cp.a;
                            break;
                        }
                    }
                }
            }

            Gradient grad = parse_gradient(box->styled_node->value("background-image"));
            if (grad.type == GradientType::None && !bg_val.empty()) {
                grad = parse_gradient(bg_val);
            }

            a *= total_opacity;
            grad.a1 *= total_opacity;
            grad.a2 *= total_opacity;
            grad.a3 *= total_opacity;
            grad.a4 *= total_opacity;

            float bg_x = box->dimensions.content.x - box->dimensions.padding.left;
            float bg_y = box->dimensions.content.y - box->dimensions.padding.top;
            float bg_w = box->dimensions.content.width + box->dimensions.padding.left + box->dimensions.padding.right;
            float bg_h = box->dimensions.content.height + box->dimensions.padding.top + box->dimensions.padding.bottom;

            if (debug_type == DebugType::Element || box == debug_box)
            {
                float margin_x = bg_x - box->dimensions.margin.left;
                float margin_y = bg_y - box->dimensions.margin.top;
                float margin_w = bg_w + box->dimensions.margin.left + box->dimensions.margin.right;
                float margin_h = bg_h + box->dimensions.margin.top + box->dimensions.margin.bottom;
                draw_rect(margin_x, margin_y, margin_w, margin_h, 1.0f, 0.5f, 0.0f, 0.4f * total_opacity, 0, combined_filter.blur, combined_filter.invert, Gradient());
                draw_rect(bg_x, bg_y, bg_w, bg_h, 0.0f, 1.0f, 0.0f, 0.4f * total_opacity, 0, combined_filter.blur, combined_filter.invert, Gradient());
                draw_rect(box->dimensions.content.x, box->dimensions.content.y,
                          box->dimensions.content.width, box->dimensions.content.height,
                          0.0f, 0.0f, 1.0f, 0.4f * total_opacity, 0, combined_filter.blur, combined_filter.invert, Gradient());
            }
            else
            {
                float radius = parse_length(box->styled_node->value("border-radius"));
                std::string rad_str = box->styled_node->value("border-radius");
                if (!rad_str.empty() && rad_str.back() == '%')
                {
                    float percent = std::stof(rad_str.substr(0, rad_str.size() - 1));
                    radius = (percent / 100.0f) * bg_w;
                }

                float border_left = box->dimensions.border.left;
                float border_top = box->dimensions.border.top;
                float border_right = box->dimensions.border.right;
                float border_bottom = box->dimensions.border.bottom;

                float full_bg_x = bg_x - border_left;
                float full_bg_y = bg_y - border_top;
                float full_bg_w = bg_w + border_left + border_right;
                float full_bg_h = bg_h + border_top + border_bottom;

                BoxShadow shadow = parse_box_shadow(box->styled_node->value("box-shadow"));
                if (shadow.active)
                {
                    float sx = full_bg_x + shadow.offsetX - shadow.spread;
                    float sy = full_bg_y + shadow.offsetY - shadow.spread;
                    float sw = full_bg_w + shadow.spread * 2.0f;
                    float sh = full_bg_h + shadow.spread * 2.0f;

                    draw_rect(sx, sy, sw, sh, shadow.r, shadow.g, shadow.b, shadow.a * total_opacity, radius + shadow.spread, shadow.blur + combined_filter.blur, combined_filter.invert, Gradient());
                }

                bool is_image_or_svg = (box->styled_node->node->tag_name == "img" || box->styled_node->node->tag_name == "svg");

                if (combined_filter.hasDropShadow && !is_image_or_svg)
                {
                    float sx = full_bg_x + combined_filter.dropShadow.offsetX - combined_filter.dropShadow.spread;
                    float sy = full_bg_y + combined_filter.dropShadow.offsetY - combined_filter.dropShadow.spread;
                    float sw = full_bg_w + combined_filter.dropShadow.spread * 2.0f;
                    float sh = full_bg_h + combined_filter.dropShadow.spread * 2.0f;

                    draw_rect(sx, sy, sw, sh, combined_filter.dropShadow.r, combined_filter.dropShadow.g, combined_filter.dropShadow.b, combined_filter.dropShadow.a * total_opacity, radius + combined_filter.dropShadow.spread, combined_filter.dropShadow.blur + combined_filter.blur, combined_filter.invert, Gradient());
                }

                bool has_border = border_top > 0 || border_bottom > 0 || border_left > 0 || border_right > 0;
                bool borders_uniform = false;
                if (has_border && radius > 0.0f)
                {
                    bool same_width = true;
                    bool same_color = true;
                    float first_width = 0;
                    float first_r = 0, first_g = 0, first_b = 0, first_a = 0;
                    bool first_set = false;

                    auto check_border = [&](float width, float br, float bg, float bb, float ba) {
                        if (width <= 0) return;
                        if (!first_set) {
                            first_width = width;
                            first_r = br; first_g = bg; first_b = bb; first_a = ba;
                            first_set = true;
                        } else {
                            if (width != first_width) same_width = false;
                            if (br != first_r || bg != first_g || bb != first_b || ba != first_a) same_color = false;
                        }
                    };

                    const auto &bc = box->dimensions.border_color;
                    check_border(border_top, bc.r[0], bc.g[0], bc.b[0], bc.a[0]);
                    check_border(border_right, bc.r[1], bc.g[1], bc.b[1], bc.a[1]);
                    check_border(border_bottom, bc.r[2], bc.g[2], bc.b[2], bc.a[2]);
                    check_border(border_left, bc.r[3], bc.g[3], bc.b[3], bc.a[3]);

                    borders_uniform = same_width && same_color;
                }

                bool all_sides_present = border_top > 0 && border_right > 0 && border_bottom > 0 && border_left > 0;

                if (borders_uniform && all_sides_present)
                {
                    const auto &bc = box->dimensions.border_color;
                    if (a > 0.0f || grad.type != GradientType::None)
                    {
                        float inner_radius = std::max(0.0f, radius - border_top);
                        draw_rect(bg_x, bg_y, bg_w, bg_h, r, g, b, a, inner_radius, combined_filter.blur, combined_filter.invert, grad);
                        draw_rect(full_bg_x, full_bg_y, full_bg_w, full_bg_h,
                                  bc.r[0], bc.g[0], bc.b[0], bc.a[0] * total_opacity,
                                  radius, combined_filter.blur, combined_filter.invert, Gradient(), border_top);
                    }
                    else
                    {
                        draw_rect(full_bg_x, full_bg_y, full_bg_w, full_bg_h,
                                  bc.r[0], bc.g[0], bc.b[0], bc.a[0] * total_opacity,
                                  radius, combined_filter.blur, combined_filter.invert, Gradient(), border_top);
                    }
                }
                else
                {
                    if (a > 0.0f || grad.type != GradientType::None)
                    {
                        draw_rect(bg_x, bg_y, bg_w, bg_h, r, g, b, a, radius, combined_filter.blur, combined_filter.invert, grad);
                    }
                }

                if (box->styled_node->node->tag_name == "img" || box->styled_node->node->tag_name == "svg") {
                    std::string src = box->styled_node->node->data.src();
                    Texture tex;
                    
                    int svg_width = static_cast<int>(box->dimensions.content.width);
                    int svg_height = static_cast<int>(box->dimensions.content.height);
                    if (svg_width <= 0) svg_width = 100;
                    if (svg_height <= 0) svg_height = 100;
                    
                    std::string svg_color = box->styled_node->value("color");
                    if (svg_color.empty()) svg_color = "black";

                    if (box->styled_node->node->tag_name == "svg") {
                        std::string content = SvgLoader::reconstruct_svg(box->styled_node->node);
                        tex = TextureManager::get().get_inline_svg_texture(content, svg_width, svg_height, svg_color);
                    } else if (!src.empty()) {
                        if (src.size() > 4 && src.substr(src.size() - 4) == ".svg") {
                            tex = TextureManager::get().get_svg_texture(src, svg_width, svg_height, svg_color);
                        } else {
                            tex = TextureManager::get().get_texture(src);
                        }
                    }

                    if (tex.id != 0) {
                        if (combined_filter.hasDropShadow) {
                            draw_image(tex.id, box->dimensions.content.x, box->dimensions.content.y,
                                       box->dimensions.content.width, box->dimensions.content.height, radius,
                                       combined_filter.dropShadow.r, combined_filter.dropShadow.g, combined_filter.dropShadow.b,
                                       combined_filter.dropShadow.a * total_opacity,
                                       combined_filter.dropShadow.offsetX, combined_filter.dropShadow.offsetY,
                                       combined_filter.dropShadow.blur, tex.width, tex.height);
                        } else {
                            draw_image(tex.id, box->dimensions.content.x, box->dimensions.content.y,
                                       box->dimensions.content.width, box->dimensions.content.height, radius);
                        }
                    }
                }

                if ((!borders_uniform || !all_sides_present) && has_border)
                {
                    const auto &bc = box->dimensions.border_color;

                    if (border_top > 0)
                    {
                        draw_rect(full_bg_x, full_bg_y, full_bg_w, border_top,
                                  bc.r[0], bc.g[0], bc.b[0], bc.a[0] * total_opacity,
                                  radius, combined_filter.blur, combined_filter.invert, Gradient());
                    }
                    if (border_bottom > 0)
                    {
                        draw_rect(full_bg_x, full_bg_y + full_bg_h - border_bottom, full_bg_w, border_bottom,
                                  bc.r[2], bc.g[2], bc.b[2], bc.a[2] * total_opacity,
                                  radius, combined_filter.blur, combined_filter.invert, Gradient());
                    }
                    if (border_left > 0)
                    {
                        draw_rect(full_bg_x, full_bg_y, border_left, full_bg_h,
                                  bc.r[3], bc.g[3], bc.b[3], bc.a[3] * total_opacity,
                                  radius, combined_filter.blur, combined_filter.invert, Gradient());
                    }
                    if (border_right > 0)
                    {
                        draw_rect(full_bg_x + full_bg_w - border_right, full_bg_y, border_right, full_bg_h,
                                  bc.r[1], bc.g[1], bc.b[1], bc.a[1] * total_opacity,
                                  radius, combined_filter.blur, combined_filter.invert, Gradient());
                    }
                }
            }
        }

        if (needs_clipping)
        {
            glEnable(GL_SCISSOR_TEST);
            int sx = (int)clip_x;
            int sy = (int)(RendererState::viewport_height - (clip_y + clip_h));
            int sw = (int)clip_w;
            int sh = (int)clip_h;

            if (old_scissor_enabled)
            {
                int ix = std::max(sx, old_scissor[0]);
                int iy = std::max(sy, old_scissor[1]);
                int iw = std::min(sx + sw, old_scissor[0] + old_scissor[2]) - ix;
                int ih = std::min(sy + sh, old_scissor[1] + old_scissor[3]) - iy;
                glScissor(ix, iy, std::max(0, iw), std::max(0, ih));
            }
            else
            {
                glScissor(sx, sy, sw, sh);
            }
        }

        auto resolve_text_shadow = [&](const std::shared_ptr<LayoutBox>& b) -> BoxShadow {
            BoxShadow shadow = parse_box_shadow(b->styled_node->value("text-shadow"));
            if (!shadow.active) {
                auto parent = b->parent.lock();
                if (parent) {
                    shadow = parse_box_shadow(parent->styled_node->value("text-shadow"));
                }
            }
            return shadow;
        };

        if (box->styled_node->node->type() == NodeType::Text)
        {
            auto text_node = std::static_pointer_cast<TextNode>(box->styled_node->node);

            float r_bg = 0.0f, g_bg = 0.0f, b_bg = 0.0f, a_bg = 0.0f;
            parse_color(box->styled_node->value("background-color"), r_bg, g_bg, b_bg, a_bg);

            if (a_bg > 0.0f)
            {
                float bg_x = box->dimensions.content.x - box->dimensions.padding.left;
                float bg_y = box->dimensions.content.y - box->dimensions.padding.top;
                float bg_w = box->dimensions.content.width + box->dimensions.padding.left + box->dimensions.padding.right;
                float bg_h = box->dimensions.content.height + box->dimensions.padding.top + box->dimensions.padding.bottom;

                float radius = parse_length(box->styled_node->value("border-radius"));
                draw_rect(bg_x, bg_y, bg_w, bg_h, r_bg, g_bg, b_bg, a_bg * total_opacity, radius, combined_filter.blur, combined_filter.invert, Gradient());
            }

            float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
            std::string color_val = box->styled_node->value("color");
            if (!color_val.empty())
            {
                parse_color(color_val, r, g, b, a);
            }

            float font_size = LayoutEngine::parse_px(box->styled_node->value("font-size"), 16.0f);
            if (font_size <= 0)
                font_size = 16.0f;

            float scale = font_size / RendererState::base_font_size;
            float scaled_ascent = RendererState::font_ascent_px * scale;
            float font_height = RendererState::font_line_height_px * scale;

            float extra_leading = std::max(0.0f, box->dimensions.content.height - font_height);
            float y_offset = extra_leading / 2.0f;

            std::string font_weight = box->styled_node->value("font-weight");
            bool is_bold = (font_weight == "bold" || font_weight == "700" || font_weight == "800" || font_weight == "900");
            std::string font_family = box->styled_node->value("font-family");
            bool is_monospace = (font_family == "monospace");

            BoxShadow text_shadow = resolve_text_shadow(box);
            if (text_shadow.active) {
                draw_text(text_node->data, box->dimensions.content.x + text_shadow.offsetX, box->dimensions.content.y + y_offset + scaled_ascent + text_shadow.offsetY, font_size, text_shadow.r, text_shadow.g, text_shadow.b, text_shadow.a * total_opacity, is_bold, text_shadow.blur + combined_filter.blur, combined_filter.invert, is_monospace);
            }

            draw_text(text_node->data, box->dimensions.content.x, box->dimensions.content.y + y_offset + scaled_ascent, font_size, r, g, b, a * total_opacity, is_bold, combined_filter.blur, combined_filter.invert, is_monospace);
            if (debug_type == DebugType::Text)
            {
                draw_rect(box->dimensions.content.x, box->dimensions.content.y,
                          box->dimensions.content.width, box->dimensions.content.height,
                          0.8f, 0.0f, 0.8f, 0.4f * total_opacity, 0.0f, 0.0f, 0.0f, Gradient());
            }
        }
        else if (auto pseudo_node = std::dynamic_pointer_cast<PseudoElementNode>(box->styled_node->node))
        {
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
            std::string color_val = box->styled_node->value("color");
            if (!color_val.empty())
            {
                parse_color(color_val, r, g, b, a);
            }

            float font_size = LayoutEngine::parse_px(box->styled_node->value("font-size"), 16.0f);
            if (font_size <= 0)
                font_size = 16.0f;

            float scale = font_size / RendererState::base_font_size;
            float scaled_ascent = RendererState::font_ascent_px * scale;
            float font_height = RendererState::font_line_height_px * scale;

            float extra_leading = std::max(0.0f, box->dimensions.content.height - font_height);
            float y_offset = extra_leading / 2.0f;

            std::string font_weight = box->styled_node->value("font-weight");
            bool is_bold = (font_weight == "bold" || font_weight == "700" || font_weight == "800" || font_weight == "900");
            std::string font_family = box->styled_node->value("font-family");
            bool is_monospace = (font_family == "monospace");

            BoxShadow text_shadow = resolve_text_shadow(box);
            if (text_shadow.active) {
                draw_text(pseudo_node->pseudo_content, box->dimensions.content.x + text_shadow.offsetX, box->dimensions.content.y + y_offset + scaled_ascent + text_shadow.offsetY, font_size, text_shadow.r, text_shadow.g, text_shadow.b, text_shadow.a * total_opacity, is_bold, text_shadow.blur + combined_filter.blur, combined_filter.invert, is_monospace);
            }

            draw_text(pseudo_node->pseudo_content, box->dimensions.content.x, box->dimensions.content.y + y_offset + scaled_ascent, font_size, r, g, b, a * total_opacity, is_bold, combined_filter.blur, combined_filter.invert, is_monospace);
        }

        auto model_with_scroll = RendererState::current_model;
        if (box->scroll_y != 0)
        {
            model_with_scroll = mat4_mul(model_with_scroll, mat4_translate(0, -box->scroll_y));
        }

        auto original_model = RendererState::current_model;
        RendererState::current_model = model_with_scroll;
        auto sorted_children = box->children;
        std::stable_sort(sorted_children.begin(), sorted_children.end(),
            [](const auto& a, const auto& b) {
                return a->z_index < b->z_index;
            });
        for (const auto &child : sorted_children)
        {
            render_box(child, total_opacity, combined_filter);
        }
        RendererState::current_model = original_model;

        if (needs_clipping)
        {
            if (old_scissor_enabled)
            {
                glScissor(old_scissor[0], old_scissor[1], old_scissor[2], old_scissor[3]);
            }
            else
            {
                glDisable(GL_SCISSOR_TEST);
            }
        }

        if (needs_clipping && overflow != "hidden" && box->total_content_height > clip_h + 1.0f)
        {
            float scrollable_height = box->total_content_height - box->dimensions.border.top - box->dimensions.border.bottom;
            Scrollbar sb = Scrollbar::from_clip(
                clip_x, clip_y, clip_w, clip_h,
                scrollable_height,
                box->scroll_y,
                total_opacity
            );
            sb.draw();
        }

        RendererState::current_model = old_model;
    }

} // namespace linweb
