#include "renderer/custom_font.h"
#include "renderer/renderer.h"
#include "utils/control_label.h"
#include "utils/file_utils.h"
#include "utils/text_transform.h"
#include "renderer/renderer_internal.h"
#include "renderer/matrix_math.h"
#include "renderer/texture_manager.h"
#include "renderer/svg_loader.h"
#include "renderer/scrollbar.h"
#include "renderer/form_renderer.h"
#include "renderer/background_renderer.h"
#include "renderer/video_surface.h"
#include "renderer/mask_renderer.h"
#include "style/colors.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>

namespace linweb {

// Draws a text run with CSS letter-spacing: each character sits after the width of the ones before it plus the spacing.
// Without spacing it is the plain draw_text call.
static void draw_text_spaced(const std::string& text, float x, float y, float size, float r, float g, float b, float a,
                             bool bold, float blur, float invert, bool mono, const CustomFont* custom, float spacing)
{
    if (spacing == 0.0f || text.empty()) {
        Renderer::draw_text(text, x, y, size, r, g, b, a, bold, blur, invert, mono, custom);
        return;
    }
    size_t index = 0;
    for (size_t i = 0; i < text.size();) {
        size_t len = 1;
        while (i + len < text.size() && (static_cast<unsigned char>(text[i + len]) & 0xC0) == 0x80) ++len;
        float offset = Renderer::measure_text(text.substr(0, i), size, bold, mono, custom) + spacing * static_cast<float>(index);
        Renderer::draw_text(text.substr(i, len), x + offset, y, size, r, g, b, a, bold, blur, invert, mono, custom);
        i += len;
        ++index;
    }
}

    static bool is_shader_background(const std::string &value)
    {
        return !shader_background_name(value).empty();
    }

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
                // Keep comma-separated color components together without
                // introducing extra commas in rgba(0, 0, 0, 0.5).
                processed.erase(std::remove_if(processed.begin() + rgba_pos, processed.begin() + end_pos,
                    [](unsigned char c) { return std::isspace(c); }), processed.begin() + end_pos);
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
            // the matching parenthesis: drop-shadow(0 0 6px rgba(255, 255, 255, 0.5)) holds another pair inside
            size_t close_paren = open_paren;
            for (int depth = 0; close_paren < s.length(); ++close_paren)
            {
                if (s[close_paren] == '(') ++depth;
                else if (s[close_paren] == ')' && --depth == 0) break;
            }
            if (close_paren >= s.length())
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
                        if (parts[0].find("right") != std::string::npos) grad.angle = 90;
                        else if (parts[0].find("bottom") != std::string::npos) grad.angle = 180;
                        else if (parts[0].find("left") != std::string::npos) grad.angle = 270;
                        else if (parts[0].find("top") != std::string::npos) grad.angle = 0;
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

                // A leading shape/size/position ("85% 80% at 34% 11%", "circle", "closest-side") is not a color stop.
                // The colors and stops that follow are what the gradient paints.
                if (!parts.empty()) {
                    const std::string& first = parts[0];
                    bool geometry = first.find(" at ") != std::string::npos || first.rfind("at ", 0) == 0 ||
                        (!first.empty() && (std::isdigit(static_cast<unsigned char>(first[0])) || first[0] == '.' || first[0] == '-')) ||
                        first.rfind("circle", 0) == 0 || first.rfind("ellipse", 0) == 0 ||
                        first.rfind("closest-", 0) == 0 || first.rfind("farthest-", 0) == 0;
                    if (geometry) parts.erase(parts.begin());
                }

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
        RendererState::background_was_visible = RendererState::background_drawn;
        RendererState::background_drawn = false;
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

            if (is_shader_background(html_bg))
            {
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                has_canvas_bg = true;
            }

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
                            if (is_shader_background(body_bg))
                            {
                                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                                glClear(GL_COLOR_BUFFER_BIT);
                                has_canvas_bg = true;
                            }
                            else
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
        draw_form_overlays(root);
    }

    void Renderer::render_box(const std::shared_ptr<LayoutBox> &box, float inherited_opacity, const Filter &inherited_filter)
    {
        if (!box || !box->styled_node || !box->styled_node->node)
            return;
        if (box->styled_node->value("visibility") == "hidden" || box->styled_node->value("visibility") == "collapse") return;

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

        // opacity multiplies down the tree: a child with its own opacity still fades with the parent that is fading
        float total_opacity = has_own_opacity ? inherited_opacity * node_opacity : inherited_opacity;

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
            std::string shader_name = shader_background_name(bg_val);
            if (shader_name.empty()) shader_name = shader_background_name(bg_color_val);
            
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

                    draw_rect(sx, sy, sw, sh, shadow.r, shadow.g, shadow.b, shadow.a * total_opacity, radius + shadow.spread, shadow.blur + combined_filter.blur, combined_filter.invert, Gradient(), 0.0f, true);
                }

                bool is_image_or_svg = (box->styled_node->node->tag_name == "img" || box->styled_node->node->tag_name == "svg");

                if (combined_filter.hasDropShadow && !is_image_or_svg && a > 0.0f)
                {
                    float sx = full_bg_x + combined_filter.dropShadow.offsetX - combined_filter.dropShadow.spread;
                    float sy = full_bg_y + combined_filter.dropShadow.offsetY - combined_filter.dropShadow.spread;
                    float sw = full_bg_w + combined_filter.dropShadow.spread * 2.0f;
                    float sh = full_bg_h + combined_filter.dropShadow.spread * 2.0f;

                    draw_rect(sx, sy, sw, sh, combined_filter.dropShadow.r, combined_filter.dropShadow.g, combined_filter.dropShadow.b, combined_filter.dropShadow.a * a, radius + combined_filter.dropShadow.spread, combined_filter.dropShadow.blur + combined_filter.blur, combined_filter.invert, Gradient(), 0.0f, true);
                }

                if (!shader_name.empty())
                {
                    draw_shader_background(shader_name, bg_x, bg_y, bg_w, bg_h, total_opacity);
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

                draw_background_image(*box, {bg_x, bg_y, bg_w, bg_h}, total_opacity);
                if (box->styled_node->node->tag_name == "img" || box->styled_node->node->tag_name == "svg") {
                    std::string src = resolve_image_src(box->styled_node->node->data.src());
                    Texture tex;
                    
                    int svg_width = static_cast<int>(box->dimensions.content.width);
                    int svg_height = static_cast<int>(box->dimensions.content.height);
                    if (svg_width <= 0) svg_width = 100;
                    if (svg_height <= 0) svg_height = 100;
                    
                    // fill (inherited, currentColor already resolved) paints the icon; color is the fallback
                    std::string svg_color = box->styled_node->value("fill");
                    if (svg_color.empty() || svg_color == "none") svg_color = box->styled_node->value("color");
                    if (svg_color.empty()) svg_color = "black";

                    if (box->styled_node->node->tag_name == "svg") {
                        std::string content = SvgLoader::reconstruct_svg(box->styled_node->node);
                        tex = TextureManager::get().get_inline_svg_texture(content, svg_width, svg_height, svg_color);
                    } else if (!src.empty()) {
                        if (src.size() > 4 && src.substr(src.size() - 4) == ".svg") {
                            tex = TextureManager::get().get_svg_texture(src, svg_width, svg_height, svg_color);
                        } else {
                            tex = TextureManager::get().get_texture(src);
                            if (tex.id == 0 && TextureManager::get().is_loading(src)) {
                                // a large picture still decoding: a soft placeholder that pulses fills its box
                                static const auto placeholder_start = std::chrono::steady_clock::now();
                                const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - placeholder_start).count();
                                const float pulse = 0.5f + 0.5f * std::sin(t * 3.2f);
                                draw_rect(box->dimensions.content.x, box->dimensions.content.y,
                                          box->dimensions.content.width, box->dimensions.content.height,
                                          0.42f, 0.62f, 1.0f, (0.10f + 0.16f * pulse) * total_opacity, radius);
                            }
                        }
                    }

                    if (tex.id != 0) {
                        if (combined_filter.hasDropShadow) {
                            draw_image(tex.id, box->dimensions.content.x, box->dimensions.content.y,
                                       box->dimensions.content.width, box->dimensions.content.height, radius,
                                       combined_filter.dropShadow.r, combined_filter.dropShadow.g, combined_filter.dropShadow.b,
                                       combined_filter.dropShadow.a * total_opacity,
                                       combined_filter.dropShadow.offsetX, combined_filter.dropShadow.offsetY,
                                       combined_filter.dropShadow.blur, tex.width, tex.height, total_opacity);
                        } else {
                            draw_image(tex.id, box->dimensions.content.x, box->dimensions.content.y,
                                       box->dimensions.content.width, box->dimensions.content.height, radius,
                                       0, 0, 0, 0, 0, 0, 0, 0, 0, total_opacity);
                        }
                    }
                }

                if (box->styled_node->node->tag_name == "video") {
                    // The picture of the video this element plays, scaled to fit the box (like object-fit: contain); the
                    // box's own background shows in the bars.
                    const auto session = RendererState::video_sessions.find(box->styled_node->node.get());
                    VideoFrameTexture frame;
                    if (session != RendererState::video_sessions.end()) frame = update_video_texture(session->second);
                    const auto& area = box->dimensions.content;
                    if (frame.id != 0 && frame.width > 0 && frame.height > 0 && area.width > 0 && area.height > 0) {
                        float scale = std::min(area.width / frame.width, area.height / frame.height);
                        float draw_w = frame.width * scale;
                        float draw_h = frame.height * scale;
                        draw_image(frame.id, area.x + (area.width - draw_w) * 0.5f, area.y + (area.height - draw_h) * 0.5f,
                                   draw_w, draw_h, radius, 0, 0, 0, 0, 0, 0, 0, 0, 0, total_opacity);
                    }
                }

                draw_form_control(box, total_opacity, radius);
                draw_list_marker(box, total_opacity);
                if (is_button_input(*box->styled_node->node)) {
                    float size = LayoutEngine::parse_px(box->styled_node->value("font-size"), 16);
                    if (size <= 0) size = 13.3f;
                    std::string label = button_input_label(*box->styled_node->node);
                    float width = measure_text(label, size);
                    float cr = 0, cg = 0, cb = 0, ca = 1;
                    parse_color(box->styled_node->value("color"), cr, cg, cb, ca);
                    float x = box->dimensions.content.x + std::max(0.0f, (box->dimensions.content.width - width) / 2);
                    float y = box->dimensions.content.y + std::max(0.0f, (box->dimensions.content.height - get_line_height(size)) / 2) + RendererState::font_ascent_px * size / RendererState::base_font_size;
                    draw_text(label, x, y, size, cr, cg, cb, ca * total_opacity);
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
            const CustomFont* custom = CustomFonts::select(box->styled_node->value("font-family"), box->styled_node->value("font-weight"), box->styled_node->value("font-style"));
            float scaled_ascent = Renderer::scaled_ascent(font_size, custom);
            float font_height = Renderer::get_line_height(font_size, custom);

            // a wrapped text is several lines tall: the leading is that of one of them
            float extra_leading = std::max(0.0f, box->dimensions.content.height / static_cast<float>(std::max(1, box->text_line_count)) - font_height);
            float y_offset = extra_leading / 2.0f;

            std::string font_weight = box->styled_node->value("font-weight");
            bool is_bold = (font_weight == "bold" || font_weight == "700" || font_weight == "800" || font_weight == "900");
            std::string font_family = box->styled_node->value("font-family");
            bool is_monospace = (font_family == "monospace");

            BoxShadow text_shadow = resolve_text_shadow(box);
            if (text_shadow.active) {
                // The shadow is drawn into its own layer and composed once, so overlapping blurred glyphs do not add up
                const std::string shadow_text = apply_text_transform(text_node->data, box->styled_node->value("text-transform"));
                const float spacing = parse_letter_spacing(box->styled_node->value("letter-spacing"));
                const float spread = text_shadow.blur + combined_filter.blur;
                const float shadow_x = box->dimensions.content.x - box->text_leading_trim + text_shadow.offsetX;
                const float shadow_y = box->dimensions.content.y + y_offset + scaled_ascent + text_shadow.offsetY;
                GLint shadow_previous_framebuffer = 0;
                if (text_shadow_layer_begin(shadow_previous_framebuffer)) {
                    // the glyphs go into the layer sharp; the blur is a separable gaussian with sigma = blur / 2 (CSS)
                    draw_text_spaced(shadow_text, shadow_x, shadow_y, font_size, text_shadow.r, text_shadow.g, text_shadow.b, text_shadow.a, is_bold, 0.0f, combined_filter.invert, is_monospace, custom, spacing);
                    text_shadow_layer_end(shadow_previous_framebuffer);
                    const float pad = spread * 1.5f + 4.0f;
                    const float left = std::min(box->dimensions.content.x - box->text_leading_trim, shadow_x) - pad;
                    const float top = std::min(box->dimensions.content.y, shadow_y - scaled_ascent) - pad;
                    const float right = std::max(box->dimensions.content.x - box->text_leading_trim + box->dimensions.content.width, shadow_x + box->dimensions.content.width) + pad;
                    const float bottom = std::max(box->dimensions.content.y + box->dimensions.content.height, shadow_y + font_height) + pad;
                    if (spread > 0.0f)
                        text_shadow_layer_blur_compose(left, top, right - left, bottom - top, spread * 0.5f, total_opacity);
                    else
                        text_shadow_layer_compose(left, top, right - left, bottom - top, total_opacity);
                } else {
                    draw_text_spaced(shadow_text, shadow_x, shadow_y, font_size, text_shadow.r, text_shadow.g, text_shadow.b, text_shadow.a * total_opacity, is_bold, spread, combined_filter.invert, is_monospace, custom, spacing);
                }
            }

            if (box->text_line_count > 1) {
                // wrapped text: each line from the left edge of the block around it, one line height below the last
                const std::vector<std::string> lines = box->text_lines();
                const float row = box->dimensions.content.height / static_cast<float>(lines.size());
                for (size_t i = 0; i < lines.size(); ++i) {
                    const float line_x = box->text_line_x(i, box->measure_text_run(lines[i]));
                    draw_text_spaced(lines[i], line_x, box->dimensions.content.y + row * static_cast<float>(i) + y_offset + scaled_ascent, font_size, r, g, b, a * total_opacity, is_bold, combined_filter.blur, combined_filter.invert, is_monospace, custom, parse_letter_spacing(box->styled_node->value("letter-spacing")));
                }
            } else
            draw_text_spaced(apply_text_transform(text_node->data, box->styled_node->value("text-transform")), box->dimensions.content.x - box->text_leading_trim, box->dimensions.content.y + y_offset + scaled_ascent, font_size, r, g, b, a * total_opacity, is_bold, combined_filter.blur, combined_filter.invert, is_monospace, custom, parse_letter_spacing(box->styled_node->value("letter-spacing")));
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
            const CustomFont* custom = CustomFonts::select(box->styled_node->value("font-family"), box->styled_node->value("font-weight"), box->styled_node->value("font-style"));
            float scaled_ascent = Renderer::scaled_ascent(font_size, custom);
            float font_height = Renderer::get_line_height(font_size, custom);

            // generated text wraps like any other: one entry per line, each with its own start
            std::vector<std::string> pseudo_lines = box->text_line_count > 1 ? box->text_lines() : std::vector<std::string>{pseudo_node->pseudo_content};
            float row_height = box->dimensions.content.height / static_cast<float>(pseudo_lines.size());
            float extra_leading = std::max(0.0f, row_height - font_height);
            float y_offset = extra_leading / 2.0f;

            std::string font_weight = box->styled_node->value("font-weight");
            bool is_bold = (font_weight == "bold" || font_weight == "700" || font_weight == "800" || font_weight == "900");
            std::string font_family = box->styled_node->value("font-family");
            bool is_monospace = (font_family == "monospace");

            BoxShadow text_shadow = resolve_text_shadow(box);
            for (size_t i = 0; i < pseudo_lines.size(); ++i) {
                const float line_x = pseudo_lines.size() > 1 ? box->text_line_x(i, box->measure_text_run(pseudo_lines[i])) : box->dimensions.content.x;
                const float line_y = box->dimensions.content.y + row_height * static_cast<float>(i) + y_offset + scaled_ascent;
                if (text_shadow.active) {
                    draw_text(pseudo_lines[i], line_x + text_shadow.offsetX, line_y + text_shadow.offsetY, font_size, text_shadow.r, text_shadow.g, text_shadow.b, text_shadow.a * total_opacity, is_bold, text_shadow.blur + combined_filter.blur, combined_filter.invert, is_monospace, custom);
                }
                draw_text(pseudo_lines[i], line_x, line_y, font_size, r, g, b, a * total_opacity, is_bold, combined_filter.blur, combined_filter.invert, is_monospace, custom);
            }
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

        // mask-image with a linear gradient: the children are drawn into a layer, then faded over the padding box
        std::vector<MaskStop> mask_stops;
        bool mask_reversed = false;
        std::string mask_value = box->styled_node->value("mask-image");
        if (mask_value.empty()) mask_value = box->styled_node->value("-webkit-mask-image");
        bool masked = needs_clipping && parse_mask_gradient(mask_value, clip_h, mask_stops, mask_reversed);
        GLint mask_previous_framebuffer = 0;
        if (masked && mask_layer_begin(mask_previous_framebuffer))
        {
            for (const auto &child : sorted_children)
            {
                render_box(child, total_opacity, combined_filter);
            }
            mask_layer_end(mask_previous_framebuffer);
            RendererState::current_model = original_model;
            mask_layer_compose(clip_x, clip_y, clip_w, clip_h, mask_stops, mask_reversed);
        }
        else
        {
            for (const auto &child : sorted_children)
            {
                render_box(child, total_opacity, combined_filter);
            }
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

        if (needs_clipping && overflow != "hidden" && box->styled_node->value("scrollbar-width") != "none" &&
            box->total_content_height > clip_h + 1.0f)
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
