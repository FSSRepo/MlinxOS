#include "renderer/mask_renderer.h"
#include "renderer/renderer_internal.h"
#include "renderer/matrix_math.h"
#include "style/colors.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace linweb {

namespace {

// Height of one band of the composed layer. The fade at the edge of a gradient changes slowly, so 2 px bands look smooth.
const float kBandHeight = 2.0f;

GLuint g_layer_fbo = 0;
GLuint g_layer_tex = 0;
int g_layer_w = 0;
int g_layer_h = 0;
int g_layer_depth = 0;
GLint g_saved_blend[4] = {GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA};

std::string mask_trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Splits on commas that are not inside parentheses (calc(...) and rgba(...) keep their commas).
std::vector<std::string> split_top_level(const std::string& s)
{
    std::vector<std::string> parts;
    int depth = 0;
    std::string current;
    for (char c : s) {
        if (c == '(') ++depth;
        if (c == ')') --depth;
        if (c == ',' && depth == 0) {
            parts.push_back(mask_trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    if (!mask_trim(current).empty()) parts.push_back(mask_trim(current));
    return parts;
}

// A stop position: N%, Npx, N, or calc(100% - Npx) / calc(100% - N%), resolved against the box height.
bool parse_stop_position(const std::string& text, float box_h, float& out)
{
    std::string t = mask_trim(text);
    if (t.compare(0, 5, "calc(") == 0 && !t.empty() && t.back() == ')') {
        std::string inner = mask_trim(t.substr(5, t.size() - 6));
        float base = 0.0f;
        if (sscanf(inner.c_str(), "100%% - %f%%", &base) == 1) { out = box_h - base * box_h / 100.0f; return true; }
        if (sscanf(inner.c_str(), "100%% - %fpx", &base) == 1) { out = box_h - base; return true; }
        if (sscanf(inner.c_str(), "100%% + %fpx", &base) == 1) { out = box_h + base; return true; }
        return false;
    }
    float value = 0.0f;
    if (t.find('%') != std::string::npos && sscanf(t.c_str(), "%f", &value) == 1) { out = value * box_h / 100.0f; return true; }
    if (sscanf(t.c_str(), "%f", &value) == 1) { out = value; return true; }
    return false;
}

float evaluate_alpha(const std::vector<MaskStop>& stops, float t)
{
    if (stops.empty()) return 1.0f;
    if (t <= stops.front().position) return stops.front().alpha;
    if (t >= stops.back().position) return stops.back().alpha;
    for (size_t i = 1; i < stops.size(); ++i) {
        if (t <= stops[i].position) {
            const MaskStop& a = stops[i - 1];
            const MaskStop& b = stops[i];
            float span = b.position - a.position;
            float f = span > 0.0f ? (t - a.position) / span : 1.0f;
            return a.alpha + (b.alpha - a.alpha) * f;
        }
    }
    return stops.back().alpha;
}

} // namespace

bool parse_mask_gradient(const std::string& value, float box_h, std::vector<MaskStop>& stops, bool& reversed)
{
    stops.clear();
    reversed = false;
    std::string v = mask_trim(value);
    const std::string prefix = "linear-gradient(";
    if (v.size() <= prefix.size() || v.compare(0, prefix.size(), prefix) != 0 || v.back() != ')') return false;

    std::vector<std::string> parts = split_top_level(v.substr(prefix.size(), v.size() - prefix.size() - 1));
    if (parts.empty()) return false;

    size_t first = 0;
    if (parts[0].compare(0, 3, "to ") == 0) {
        reversed = parts[0] == "to top";
        first = 1;
    } else if (parts[0].find("deg") != std::string::npos) {
        reversed = false;   // 180deg is "to bottom" in CSS
        first = 1;
    }

    // Each stop is "<color> [<position>]". Stops without a position are spaced evenly between their neighbours.
    std::vector<float> positions;
    for (size_t i = first; i < parts.size(); ++i) {
        const std::string& part = parts[i];
        size_t split = std::string::npos;
        int depth = 0;
        for (size_t k = 0; k < part.size(); ++k) {
            if (part[k] == '(') ++depth;
            else if (part[k] == ')') --depth;
            else if (part[k] == ' ' && depth == 0) { split = k; break; }
        }
        std::string color_text = split == std::string::npos ? part : part.substr(0, split);
        std::string position_text = split == std::string::npos ? "" : mask_trim(part.substr(split));

        Color c = parse_color_str(color_text);
        MaskStop stop;
        stop.alpha = c.a;
        stops.push_back(stop);

        float position = 0.0f;
        if (!position_text.empty() && parse_stop_position(position_text, box_h, position)) positions.push_back(position);
        else positions.push_back(NAN);
    }
    if (stops.size() < 2) return false;

    // Fill missing positions: first 0, last box height, the rest evenly between the known ones.
    if (std::isnan(positions.front())) positions.front() = 0.0f;
    if (std::isnan(positions.back())) positions.back() = box_h;
    for (size_t i = 0; i < positions.size(); ++i) {
        if (!std::isnan(positions[i])) continue;
        size_t j = i;
        while (std::isnan(positions[j])) ++j;
        float from = positions[i - 1];
        float to = positions[j];
        float steps = static_cast<float>(j - i + 1);
        for (size_t k = i; k < j; ++k) positions[k] = from + (to - from) * static_cast<float>(k - i + 1) / steps;
    }
    for (size_t i = 0; i < stops.size(); ++i) stops[i].position = positions[i];
    return true;
}

bool mask_layer_begin(GLint& previous_framebuffer)
{
    if (g_layer_depth > 0) return false;   // nested mask: draw the children without a layer

    int w = RendererState::viewport_width;
    int h = RendererState::viewport_height;
    if (!g_layer_fbo) glGenFramebuffers(1, &g_layer_fbo);
    if (!g_layer_tex) glGenTextures(1, &g_layer_tex);
    if (g_layer_w != w || g_layer_h != h) {
        glBindTexture(GL_TEXTURE_2D, g_layer_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g_layer_w = w;
        g_layer_h = h;
    }

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);
    if (g_layer_depth == 0) {
        glGetIntegerv(GL_BLEND_SRC_RGB, &g_saved_blend[0]);
        glGetIntegerv(GL_BLEND_DST_RGB, &g_saved_blend[1]);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &g_saved_blend[2]);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &g_saved_blend[3]);
    }
    // inside the layer the alpha accumulates (a + dst * (1 - a)) and the colour is kept premultiplied
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBindFramebuffer(GL_FRAMEBUFFER, g_layer_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_layer_tex, 0);
    glViewport(0, 0, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ++g_layer_depth;
    return true;
}

void mask_layer_end(GLint previous_framebuffer)
{
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
    glViewport(0, 0, RendererState::viewport_width, RendererState::viewport_height);
    --g_layer_depth;
    if (g_layer_depth == 0)
        glBlendFuncSeparate(static_cast<GLenum>(g_saved_blend[0]), static_cast<GLenum>(g_saved_blend[1]),
                            static_cast<GLenum>(g_saved_blend[2]), static_cast<GLenum>(g_saved_blend[3]));
}

void mask_layer_compose(float x, float y, float w, float h, const std::vector<MaskStop>& stops, bool reversed)
{
    if (!RendererState::program_image || RendererState::vbo_color == 0 || g_layer_tex == 0 || w <= 0.0f || h <= 0.0f) return;

    const float vw = static_cast<float>(g_layer_w);
    const float vh = static_cast<float>(g_layer_h);
    const float u0 = x / vw;
    const float u1 = (x + w) / vw;

    glUseProgram(RendererState::program_image);
    auto identity = mat4_identity();
    glUniformMatrix4fv(RendererState::program_image_u_model, 1, GL_FALSE, identity.data());
    glUniform1f(RendererState::program_image_u_radius, 0.0f);
    glUniform2f(RendererState::program_image_u_size, w, h);
    if (RendererState::program_image_u_shadow_offset >= 0) glUniform2f(RendererState::program_image_u_shadow_offset, 0.0f, 0.0f);
    if (RendererState::program_image_u_shadow_blur >= 0) glUniform1f(RendererState::program_image_u_shadow_blur, 0.0f);
    if (RendererState::program_image_u_shadow_color >= 0) glUniform4f(RendererState::program_image_u_shadow_color, 0.0f, 0.0f, 0.0f, 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_layer_tex);
    glUniform1i(RendererState::program_image_u_tex, 0);
    const GLint opacity_location = glGetUniformLocation(RendererState::program_image, "uOpacity");

    glBindBuffer(GL_ARRAY_BUFFER, RendererState::vbo_color);
    glEnableVertexAttribArray(RendererState::program_image_a_pos);
    glVertexAttribPointer(RendererState::program_image_a_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(RendererState::program_image_a_uv);
    glVertexAttribPointer(RendererState::program_image_a_uv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    // The layer keeps premultiplied colour. Each band is multiplied by its opacity: the colour through the constant
    // blend colour (the image shader only applies uOpacity to alpha), the alpha through the shader output.
    GLint blend_src_rgb = GL_SRC_ALPHA, blend_dst_rgb = GL_ONE_MINUS_SRC_ALPHA;
    glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb);
    glBlendFuncSeparate(GL_CONSTANT_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    const float bottom = y + h;
    for (float top = y; top < bottom; top += kBandHeight) {
        const float band_bottom = std::min(top + kBandHeight, bottom);
        const float middle = (top + band_bottom) * 0.5f - y;
        const float t = reversed ? h - middle : middle;
        const float alpha = evaluate_alpha(stops, t);
        if (alpha <= 0.002f) continue;

        // the layer has its origin at the bottom: layout row y is texture row (height - y)
        const float v_top = (vh - top) / vh;
        const float v_bottom = (vh - band_bottom) / vh;
        glUniform1f(opacity_location, alpha);
        glBlendColor(alpha, alpha, alpha, alpha);   // all four: a driver that reads CONSTANT_ALPHA from the red channel (VirtualBox SVGA) gets the same factor

        float verts[] = {
            x,     top,         u0, v_top,
            x + w, top,         u1, v_top,
            x + w, band_bottom, u1, v_bottom,

            x,     top,         u0, v_top,
            x + w, band_bottom, u1, v_bottom,
            x,     band_bottom, u0, v_bottom
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glBlendFunc(static_cast<GLenum>(blend_src_rgb), static_cast<GLenum>(blend_dst_rgb));
    glDisableVertexAttribArray(RendererState::program_image_a_pos);
    glDisableVertexAttribArray(RendererState::program_image_a_uv);
    glUseProgram(0);
}

} // namespace linweb

namespace linweb {

namespace {
GLuint g_shadow_fbo = 0;
GLuint g_shadow_tex = 0;
int g_shadow_w = 0;
int g_shadow_h = 0;
GLint g_shadow_saved_blend[4] = {0, 0, 0, 0};   // src rgb, dst rgb, src alpha, dst alpha
GLint g_shadow_saved_equation[2] = {0, 0};      // rgb, alpha
} // namespace

bool text_shadow_layer_begin(GLint& previous_framebuffer)
{
    int w = RendererState::viewport_width;
    int h = RendererState::viewport_height;
    if (!g_shadow_fbo) glGenFramebuffers(1, &g_shadow_fbo);
    if (!g_shadow_tex) glGenTextures(1, &g_shadow_tex);
    if (g_shadow_w != w || g_shadow_h != h) {
        glBindTexture(GL_TEXTURE_2D, g_shadow_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g_shadow_w = w;
        g_shadow_h = h;
    }

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);
    glGetIntegerv(GL_BLEND_SRC_RGB, &g_shadow_saved_blend[0]);
    glGetIntegerv(GL_BLEND_DST_RGB, &g_shadow_saved_blend[1]);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &g_shadow_saved_blend[2]);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &g_shadow_saved_blend[3]);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &g_shadow_saved_equation[0]);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &g_shadow_saved_equation[1]);

    glBindFramebuffer(GL_FRAMEBUFFER, g_shadow_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_shadow_tex, 0);
    glViewport(0, 0, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // colour is written as it is (the glyph colour), alpha keeps the largest coverage of the glyphs
    glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ONE);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);
    return true;
}

GLuint text_shadow_layer_texture() { return g_shadow_tex; }
int text_shadow_layer_width() { return g_shadow_w; }
int text_shadow_layer_height() { return g_shadow_h; }

void text_shadow_layer_end(GLint previous_framebuffer)
{
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
    glViewport(0, 0, RendererState::viewport_width, RendererState::viewport_height);
    glBlendEquationSeparate(static_cast<GLenum>(g_shadow_saved_equation[0]), static_cast<GLenum>(g_shadow_saved_equation[1]));
    glBlendFuncSeparate(static_cast<GLenum>(g_shadow_saved_blend[0]), static_cast<GLenum>(g_shadow_saved_blend[1]),
                        static_cast<GLenum>(g_shadow_saved_blend[2]), static_cast<GLenum>(g_shadow_saved_blend[3]));
}

void text_shadow_layer_compose(float x, float y, float w, float h, float opacity)
{
    if (!RendererState::program_image || RendererState::vbo_color == 0 || g_shadow_tex == 0 || w <= 0.0f || h <= 0.0f) return;

    const float vw = static_cast<float>(g_shadow_w);
    const float vh = static_cast<float>(g_shadow_h);
    const float u0 = x / vw, u1 = (x + w) / vw;
    const float v_top = (vh - y) / vh, v_bottom = (vh - (y + h)) / vh;

    glUseProgram(RendererState::program_image);
    auto identity = mat4_identity();
    glUniformMatrix4fv(RendererState::program_image_u_model, 1, GL_FALSE, identity.data());
    glUniform1f(RendererState::program_image_u_radius, 0.0f);
    glUniform2f(RendererState::program_image_u_size, w, h);
    if (RendererState::program_image_u_shadow_offset >= 0) glUniform2f(RendererState::program_image_u_shadow_offset, 0.0f, 0.0f);
    if (RendererState::program_image_u_shadow_blur >= 0) glUniform1f(RendererState::program_image_u_shadow_blur, 0.0f);
    if (RendererState::program_image_u_shadow_color >= 0) glUniform4f(RendererState::program_image_u_shadow_color, 0.0f, 0.0f, 0.0f, 0.0f);
    glUniform1f(glGetUniformLocation(RendererState::program_image, "uOpacity"), opacity);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_shadow_tex);
    glUniform1i(RendererState::program_image_u_tex, 0);

    // the glyph shader writes premultiplied colour: composed with ONE, and the opacity scales the colour through the
    // constant blend colour (the image shader only applies uOpacity to alpha)
    GLint src_rgb = GL_SRC_ALPHA, dst_rgb = GL_ONE_MINUS_SRC_ALPHA, src_a = GL_ONE, dst_a = GL_ONE_MINUS_SRC_ALPHA;
    glGetIntegerv(GL_BLEND_SRC_RGB, &src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &src_a);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dst_a);
    glBlendColor(opacity, opacity, opacity, opacity);   // all four, as in mask_layer_compose
    glBlendFuncSeparate(GL_CONSTANT_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    float verts[] = {
        x,     y,     u0, v_top,
        x + w, y,     u1, v_top,
        x + w, y + h, u1, v_bottom,

        x,     y,     u0, v_top,
        x + w, y + h, u1, v_bottom,
        x,     y + h, u0, v_bottom
    };
    glBindBuffer(GL_ARRAY_BUFFER, RendererState::vbo_color);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
    glEnableVertexAttribArray(RendererState::program_image_a_pos);
    glVertexAttribPointer(RendererState::program_image_a_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(RendererState::program_image_a_uv);
    glVertexAttribPointer(RendererState::program_image_a_uv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBlendFuncSeparate(static_cast<GLenum>(src_rgb), static_cast<GLenum>(dst_rgb), static_cast<GLenum>(src_a), static_cast<GLenum>(dst_a));
    glDisableVertexAttribArray(RendererState::program_image_a_pos);
    glDisableVertexAttribArray(RendererState::program_image_a_uv);
    glUseProgram(0);
}

} // namespace linweb
