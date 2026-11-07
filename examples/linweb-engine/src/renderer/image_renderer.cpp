#include "renderer/image_renderer.h"
#include "renderer/renderer_internal.h"
#include <algorithm>

namespace linweb {

void Renderer::draw_image(GLuint texture_id, float x, float y, float width, float height, float radius,
                           float shadow_r, float shadow_g, float shadow_b, float shadow_a,
                           float shadow_offset_x, float shadow_offset_y, float shadow_blur,
                           int tex_width, int tex_height, float opacity) {
    if (!RendererState::program_image || RendererState::vbo_color == 0 || texture_id == 0) return;
    if (width <= 0.0f || height <= 0.0f) return;

    float max_radius = std::min(width, height) * 0.5f;
    if (radius > max_radius) radius = max_radius;

    glUseProgram(RendererState::program_image);
    glUniform1f(glGetUniformLocation(RendererState::program_image, "uOpacity"), opacity);
    glUniformMatrix4fv(RendererState::program_image_u_model, 1, GL_FALSE, RendererState::current_model.data());
    glUniform2f(RendererState::program_image_u_size, width, height);
    glUniform1f(RendererState::program_image_u_radius, radius);

    if (RendererState::program_image_u_shadow_offset >= 0) {
        float off_x = 0.0f, off_y = 0.0f;
        if (width > 0 && height > 0) {
            off_x = shadow_offset_x / width;
            off_y = shadow_offset_y / height;
        }
        glUniform2f(RendererState::program_image_u_shadow_offset, off_x, off_y);
    }
    if (RendererState::program_image_u_shadow_blur >= 0) {
        glUniform1f(RendererState::program_image_u_shadow_blur, shadow_blur);
    }
    if (RendererState::program_image_u_shadow_color >= 0) {
        glUniform4f(RendererState::program_image_u_shadow_color, shadow_r, shadow_g, shadow_b, shadow_a);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glUniform1i(RendererState::program_image_u_tex, 0);

    float pad = shadow_a > 0 ? std::max(0.0f, shadow_blur * 2.0f) + std::max(std::abs(shadow_offset_x), std::abs(shadow_offset_y)) : 0.0f;
    float u0 = -pad / width, v0 = -pad / height, u1 = 1.0f + pad / width, v1 = 1.0f + pad / height;
    float verts[] = {
        x - pad, y - pad, u0, v0,
        x + width + pad, y - pad, u1, v0,
        x + width + pad, y + height + pad, u1, v1,

        x - pad, y - pad, u0, v0,
        x + width + pad, y + height + pad, u1, v1,
        x - pad, y + height + pad, u0, v1
    };

    glBindBuffer(GL_ARRAY_BUFFER, RendererState::vbo_color);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);

    glEnableVertexAttribArray(RendererState::program_image_a_pos);
    glVertexAttribPointer(RendererState::program_image_a_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(RendererState::program_image_a_uv);
    glVertexAttribPointer(RendererState::program_image_a_uv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glDisableVertexAttribArray(RendererState::program_image_a_pos);
    glDisableVertexAttribArray(RendererState::program_image_a_uv);
    glUseProgram(0);
}

} // namespace linweb
