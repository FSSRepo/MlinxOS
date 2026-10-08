#include "renderer/text_renderer.h"
#include "renderer/renderer_internal.h"

namespace linweb {

float Renderer::measure_text(const std::string& text, float font_size, bool is_bold, bool is_monospace) {
    float xpos = 0;
    float scale = font_size / RendererState::base_font_size;
    for (char c : text) {
        if (c < 32 || c >= 128) continue;
        stbtt_bakedchar *b;
        if (is_monospace)
            b = &RendererState::cdata_mono[c - 32];
        else if (is_bold)
            b = &RendererState::cdata_bold[c - 32];
        else
            b = &RendererState::cdata[c - 32];
        xpos += b->xadvance * scale;
    }
    return xpos;
}

float Renderer::get_line_height(float font_size) {
    return RendererState::font_line_height_px * (font_size / RendererState::base_font_size);
}

void Renderer::draw_text(const std::string& text, float x, float y, float font_size, float r, float g, float b, float a, bool is_bold, float blur, float invert, bool is_monospace) {
    if (!RendererState::program_text || RendererState::vbo_text == 0) return;
    if (!is_monospace && RendererState::ftex == 0) return;
    if (is_monospace && RendererState::ftex_mono == 0) return;

    float xpos = x;
    float ypos = y;
    float scale = font_size / RendererState::base_font_size;

    std::vector<float> verts;
    verts.reserve(text.size() * 6 * 8);

    for (char c : text) {
        if (c < 32 || c >= 128) continue;
        stbtt_bakedchar *bc;
        if (is_monospace)
            bc = &RendererState::cdata_mono[c - 32];
        else if (is_bold)
            bc = &RendererState::cdata_bold[c - 32];
        else
            bc = &RendererState::cdata[c - 32];
        
        float x0 = xpos + bc->xoff * scale;
        float y0 = ypos + bc->yoff * scale;
        float x1 = x0 + (bc->x1 - bc->x0) * scale;
        float y1 = y0 + (bc->y1 - bc->y0) * scale;

        float s0 = bc->x0 / 1024.0f;
        float t0 = bc->y0 / 1024.0f;
        float s1 = bc->x1 / 1024.0f;
        float t1 = bc->y1 / 1024.0f;

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
    GLuint tex_id;
    if (is_monospace)
        tex_id = RendererState::ftex_mono;
    else if (is_bold)
        tex_id = RendererState::ftex_bold;
    else
        tex_id = RendererState::ftex;
    glBindTexture(GL_TEXTURE_2D, tex_id);
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
