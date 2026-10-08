#include "renderer/primitive_renderer.h"
#include "renderer/renderer_internal.h"
#include <algorithm>

namespace linweb {

void Renderer::draw_rect(float x, float y, float width, float height, float r, float g, float b, float a, float radius, float blur, float invert, const Gradient& gradient, float border_width) {
    if (!RendererState::program_color || RendererState::vbo_color == 0) return;
    if (width <= 0.0f || height <= 0.0f) return;

    float max_radius = std::min(width, height) * 0.5f;
    if (radius > max_radius) radius = max_radius;

    glUseProgram(RendererState::program_color);
    glUniformMatrix4fv(RendererState::program_color_u_model, 1, GL_FALSE, RendererState::current_model.data());
    glUniform2f(RendererState::program_color_u_size, width, height);
    glUniform1f(RendererState::program_color_u_radius, radius);
    glUniform1f(RendererState::program_color_u_blur, blur);
    glUniform1f(RendererState::program_color_u_invert, invert);
    glUniform1f(RendererState::program_color_u_border_width, border_width);

    glUniform1i(RendererState::program_color_u_grad_type, (int)gradient.type);
    if (gradient.type != GradientType::None) {
        glUniform4f(RendererState::program_color_u_grad_c1, gradient.r1, gradient.g1, gradient.b1, gradient.a1);
        glUniform4f(RendererState::program_color_u_grad_c2, gradient.r2, gradient.g2, gradient.b2, gradient.a2);
        glUniform4f(RendererState::program_color_u_grad_c3, gradient.r3, gradient.g3, gradient.b3, gradient.a3);
        glUniform4f(RendererState::program_color_u_grad_c4, gradient.r4, gradient.g4, gradient.b4, gradient.a4);
        glUniform1f(RendererState::program_color_u_grad_s1, gradient.s1);
        glUniform1f(RendererState::program_color_u_grad_s2, gradient.s2);
        glUniform1f(RendererState::program_color_u_grad_s3, gradient.s3);
        glUniform1f(RendererState::program_color_u_grad_s4, gradient.s4);
        glUniform1i(RendererState::program_color_u_grad_count, gradient.color_count);
        glUniform1f(RendererState::program_color_u_grad_angle, gradient.angle);
    }

    float margin = blur * 3.0f;
    if (margin < 2.0f) margin = 2.0f;
    float x0 = x - margin;
    float y0 = y - margin;
    float x1 = x + width + margin;
    float y1 = y + height + margin;

    float u0 = -margin / width;
    float v0 = -margin / height;
    float u1 = 1.0f + margin / width;
    float v1 = 1.0f + margin / height;

    float verts[] = {
        x0, y0, r, g, b, a, u0, v0,
        x1, y0, r, g, b, a, u1, v0,
        x1, y1, r, g, b, a, u1, v1,

        x0, y0, r, g, b, a, u0, v0,
        x1, y1, r, g, b, a, u1, v1,
        x0, y1, r, g, b, a, u0, v1
    };

    glBindBuffer(GL_ARRAY_BUFFER, RendererState::vbo_color);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);

    glEnableVertexAttribArray(RendererState::program_color_a_pos);
    glVertexAttribPointer(RendererState::program_color_a_pos, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(RendererState::program_color_a_color);
    glVertexAttribPointer(RendererState::program_color_a_color, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(RendererState::program_color_a_uv);
    glVertexAttribPointer(RendererState::program_color_a_uv, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glDisableVertexAttribArray(RendererState::program_color_a_pos);
    glDisableVertexAttribArray(RendererState::program_color_a_color);
    glDisableVertexAttribArray(RendererState::program_color_a_uv);
    glUseProgram(0);
}

} // namespace linweb
