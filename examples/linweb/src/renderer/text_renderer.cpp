#include <cmath>
#include "renderer/text_renderer.h"
#include "renderer/renderer_internal.h"
#include "renderer/custom_font.h"
#include "utils/text_encoding.h"

namespace linweb {

// The baked glyphs of the face a text run uses: a loaded @font-face face, or one of the built-in faces
static const stbtt_bakedchar* glyphs_of(bool is_bold, bool is_monospace, const CustomFont* custom) {
    if (custom) return custom->glyphs;
    if (is_monospace) return RendererState::cdata_mono;
    return is_bold ? RendererState::cdata_bold : RendererState::cdata;
}

static GLuint texture_of(bool is_bold, bool is_monospace, const CustomFont* custom) {
    if (custom) return custom->texture;
    if (is_monospace) return RendererState::ftex_mono;
    return is_bold ? RendererState::ftex_bold : RendererState::ftex;
}

float Renderer::measure_text(const std::string& text, float font_size, bool is_bold, bool is_monospace, const CustomFont* custom) {
    float xpos = 0;
    float scale = font_size / RendererState::base_font_size;
    const stbtt_bakedchar* glyphs = glyphs_of(is_bold, is_monospace, custom);
    for (size_t pos = 0; pos < text.size();) {
        uint32_t c = next_codepoint(text, pos);
        if (c < 32) continue;
        if (c > 255) c = '?';
        const stbtt_bakedchar *b = &glyphs[c - 32];
        xpos += b->xadvance * scale;
    }
    return xpos;
}

float Renderer::font_ascent(const CustomFont* custom) {
    return custom ? custom->ascent_px : RendererState::font_ascent_px;
}

float Renderer::get_line_height(float font_size, const CustomFont* custom) {
    const float scale = font_size / RendererState::base_font_size;
    const float descent = custom ? custom->descent_px : RendererState::font_descent_px;
    const float gap = custom ? custom->gap_px : RendererState::font_gap_px;
    const float ascent = Renderer::font_ascent(custom);
    if (ascent <= 0.0f) return (custom ? custom->line_height_px : RendererState::font_line_height_px) * scale;
    return std::round(ascent * scale) + std::round(descent * scale) + std::round(gap * scale);
}

// Where the baseline is below the top of a line of text at this size: the rounded ascent, and half of the rounded gap
float Renderer::scaled_ascent(float font_size, const CustomFont* custom) {
    const float scale = font_size / RendererState::base_font_size;
    const float gap = custom ? custom->gap_px : RendererState::font_gap_px;
    return std::round(Renderer::font_ascent(custom) * scale) + std::round(gap * scale) / 2.0f;
}

void Renderer::draw_text(const std::string& text, float x, float y, float font_size, float r, float g, float b, float a, bool is_bold, float blur, float invert, bool is_monospace, const CustomFont* custom) {
    if (!RendererState::program_text || RendererState::vbo_text == 0) return;
    if (!custom && !is_monospace && RendererState::ftex == 0) return;
    if (!custom && is_monospace && RendererState::ftex_mono == 0) return;
    const stbtt_bakedchar* glyphs = glyphs_of(is_bold, is_monospace, custom);

    float xpos = x;
    float ypos = y;
    float scale = font_size / RendererState::base_font_size;

    std::vector<float> verts;
    verts.reserve(text.size() * 6 * 8);

    for (size_t pos = 0; pos < text.size();) {
        uint32_t c = next_codepoint(text, pos);
        if (c < 32) continue;
        if (c > 255) c = '?';
        const stbtt_bakedchar* bc = &glyphs[c - 32];
        
        float x0 = xpos + bc->xoff * scale;
        float y0 = ypos + bc->yoff * scale;
        float x1 = x0 + (bc->x1 - bc->x0) * scale;
        float y1 = y0 + (bc->y1 - bc->y0) * scale;

        float s0 = bc->x0 / 1024.0f;
        float t0 = bc->y0 / 1024.0f;
        float s1 = bc->x1 / 1024.0f;
        float t1 = bc->y1 / 1024.0f;

        // A blurred glyph (text-shadow) spreads past its own box: the quad grows by the blur radius, which the
        // fragment shader spans as blur * 0.0015 of the 1024 px atlas, so the spread is blur * 1.536 atlas pixels.
        if (blur > 0.1f) {
            float pad_px = blur * 1.6f * scale;
            float pad_uv = blur * 1.6f / 1024.0f;
            x0 -= pad_px; y0 -= pad_px; x1 += pad_px; y1 += pad_px;
            s0 -= pad_uv; t0 -= pad_uv; s1 += pad_uv; t1 += pad_uv;
        }

        float v[] = {
            x0, y0, s0, t0, r, g, b, a,
            x1, y0, s1, t0, r, g, b, a,
            x1, y1, s1, t1, r, g, b, a,

            x0, y0, s0, t0, r, g, b, a,
            x1, y1, s1, t1, r, g, b, a,
            x0, y1, s0, t1, r, g, b, a
        };
        verts.insert(verts.end(), std::begin(v), std::end(v));

        xpos += bc->xadvance * scale;
    }

    if (verts.empty()) return;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_of(is_bold, is_monospace, custom));
    glUseProgram(RendererState::program_text);
    glUniformMatrix4fv(RendererState::program_text_u_model, 1, GL_FALSE, RendererState::current_model.data());
    glUniform1f(RendererState::program_text_u_blur, blur);
    glUniform1f(RendererState::program_text_u_invert, invert);

    glBindBuffer(GL_ARRAY_BUFFER, RendererState::vbo_text);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STREAM_DRAW);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glEnableVertexAttribArray(RendererState::program_text_a_pos);
    glEnableVertexAttribArray(RendererState::program_text_a_uv);
    glEnableVertexAttribArray(RendererState::program_text_a_color);
    glVertexAttribPointer(RendererState::program_text_a_pos, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glVertexAttribPointer(RendererState::program_text_a_uv, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
    glVertexAttribPointer(RendererState::program_text_a_color, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size() / 8));

    glDisableVertexAttribArray(RendererState::program_text_a_pos);
    glDisableVertexAttribArray(RendererState::program_text_a_uv);
    glDisableVertexAttribArray(RendererState::program_text_a_color);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

} // namespace linweb
