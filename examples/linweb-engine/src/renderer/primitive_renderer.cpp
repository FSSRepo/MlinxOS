#include "renderer/primitive_renderer.h"
#include "renderer/renderer_internal.h"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace linweb {

std::string shader_background_name(const std::string& value) {
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    const std::string name = value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    for (const auto& background : RendererState::backgrounds) {
        if (background.spec.name == name) return name;
    }
    return std::string();
}

void Renderer::register_shader_background(ShaderBackgroundSpec spec) {
    for (auto& background : RendererState::backgrounds) {
        if (background.spec.name != spec.name) continue;
        if (background.program) glDeleteProgram(background.program);   // a new shader replaces the old one
        background = RendererState::RegisteredBackground();
        background.spec = std::move(spec);
        return;
    }
    RendererState::RegisteredBackground entry;
    entry.spec = std::move(spec);
    RendererState::backgrounds.push_back(std::move(entry));
}

bool Renderer::animated_background_visible() {
    return RendererState::background_was_visible;
}

void Renderer::draw_shader_background(const std::string& name, float x, float y, float width, float height, float opacity) {
    if (RendererState::vbo_color == 0 || width <= 0.0f || height <= 0.0f) return;
    RendererState::RegisteredBackground* background = nullptr;
    for (auto& candidate : RendererState::backgrounds) {
        if (candidate.spec.name == name) { background = &candidate; break; }
    }
    if (!background || background->failed) return;

    if (!background->program) {
        // the first time it is drawn: compile it (the GL context is current while drawing)
#ifdef WESTON_PLATFORM
        const std::string& fragment = background->spec.fragment_gles;
#else
        const std::string& fragment = background->spec.fragment_gl;
#endif
        GLuint vs = compile_shader(GL_VERTEX_SHADER, shader_background_vertex_source());
        GLuint fs = fragment.empty() ? 0 : compile_shader(GL_FRAGMENT_SHADER, fragment.c_str());
        if (vs && fs) background->program = link_program(vs, fs);
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        if (!background->program) {
            background->failed = true;
            std::cerr << "shader background " << name << " did not compile" << std::endl;
            return;
        }
        background->u_proj = glGetUniformLocation(background->program, "uProj");
        background->u_model = glGetUniformLocation(background->program, "uModel");
        background->a_pos = glGetAttribLocation(background->program, "aPos");
        background->a_uv = glGetAttribLocation(background->program, "aUV");
    }

    static const auto start_time = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();

    glUseProgram(background->program);
    glUniformMatrix4fv(background->u_proj, 1, GL_FALSE, RendererState::proj.data());
    glUniformMatrix4fv(background->u_model, 1, GL_FALSE, RendererState::current_model.data());
    if (background->spec.set_uniforms) background->spec.set_uniforms(background->program, x, y, width, height, opacity, elapsed);
    if (background->spec.animated) RendererState::background_drawn = true;

    float verts[] = {
        x, y, 0.0f, 0.0f,
        x + width, y, 1.0f, 0.0f,
        x + width, y + height, 1.0f, 1.0f,

        x, y, 0.0f, 0.0f,
        x + width, y + height, 1.0f, 1.0f,
        x, y + height, 0.0f, 1.0f
    };

    glBindBuffer(GL_ARRAY_BUFFER, RendererState::vbo_color);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);

    glEnableVertexAttribArray(background->a_pos);
    glVertexAttribPointer(background->a_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(background->a_uv);
    glVertexAttribPointer(background->a_uv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glDisableVertexAttribArray(background->a_pos);
    glDisableVertexAttribArray(background->a_uv);
    glUseProgram(0);
}

void Renderer::draw_rect(float x, float y, float width, float height, float r, float g, float b, float a, float radius, float blur, float invert, const Gradient& gradient, float border_width, bool is_shadow) {
    if (!RendererState::program_color || RendererState::vbo_color == 0) return;
    if (width <= 0.0f || height <= 0.0f) return;

    float max_radius = std::min(width, height) * 0.5f;
    if (radius > max_radius) radius = max_radius;

    glUseProgram(RendererState::program_color);
    glUniformMatrix4fv(RendererState::program_color_u_model, 1, GL_FALSE, RendererState::current_model.data());
    glUniform2f(RendererState::program_color_u_size, width, height);
    glUniform1f(RendererState::program_color_u_radius, radius);
    glUniform1f(RendererState::program_color_u_blur, blur);
    glUniform1f(RendererState::program_color_u_shadow, is_shadow ? 1.0f : 0.0f);
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
