#include "renderer/renderer_core.h"
#include "renderer/renderer_internal.h"
#include "renderer/shader_manager.h"
#include "renderer/font_manager.h"
#include "renderer/shaders.h"
#include "renderer/matrix_math.h"
#include "style/colors.h"
#include <algorithm>
#include <iostream>

namespace linweb {

    // Initialize static members of RendererState
    unsigned char RendererState::font_buffer[1 << 20];
    unsigned char RendererState::font_bold_buffer[1 << 20];
    unsigned char RendererState::font_mono_buffer[1 << 20];
    stbtt_bakedchar RendererState::cdata[96];
    stbtt_bakedchar RendererState::cdata_bold[96];
    stbtt_bakedchar RendererState::cdata_mono[96];
    GLuint RendererState::ftex = 0;
    GLuint RendererState::ftex_bold = 0;
    GLuint RendererState::ftex_mono = 0;
    stbtt_fontinfo RendererState::font_info;
    stbtt_fontinfo RendererState::font_bold_info;
    stbtt_fontinfo RendererState::font_mono_info;
    float RendererState::base_font_size = 64.0f;
    float RendererState::font_ascent_px = 18.0f;
    float RendererState::font_line_height_px = 24.0f;
    int RendererState::viewport_width = 1;
    int RendererState::viewport_height = 1;
    std::array<float, 16> RendererState::proj{};
    std::array<float, 16> RendererState::current_model = mat4_identity();
    
    // Renderer static members initialization
    DebugType Renderer::debug_type = DebugType::None;
    std::shared_ptr<LayoutBox> Renderer::debug_box = nullptr;

    GLuint RendererState::program_color = 0;
    GLint RendererState::program_color_u_proj = -1;
    GLint RendererState::program_color_u_model = -1;
    GLint RendererState::program_color_u_size = -1;
    GLint RendererState::program_color_u_radius = -1;
    GLint RendererState::program_color_u_blur = -1;
    GLint RendererState::program_color_u_invert = -1;
    GLint RendererState::program_color_u_grad_type = -1;
    GLint RendererState::program_color_u_grad_c1 = -1;
    GLint RendererState::program_color_u_grad_c2 = -1;
    GLint RendererState::program_color_u_grad_c3 = -1;
    GLint RendererState::program_color_u_grad_c4 = -1;
    GLint RendererState::program_color_u_grad_s1 = -1;
    GLint RendererState::program_color_u_grad_s2 = -1;
    GLint RendererState::program_color_u_grad_s3 = -1;
    GLint RendererState::program_color_u_grad_s4 = -1;
    GLint RendererState::program_color_u_grad_count = -1;
    GLint RendererState::program_color_u_grad_angle = -1;
    GLint RendererState::program_color_u_border_width = -1;
    GLint RendererState::program_color_a_pos = -1;
    GLint RendererState::program_color_a_color = -1;
    GLint RendererState::program_color_a_uv = -1;
    GLuint RendererState::vbo_color = 0;
    GLuint RendererState::program_text = 0;
    GLint RendererState::program_text_u_proj = -1;
    GLint RendererState::program_text_u_model = -1;
    GLint RendererState::program_text_u_tex = -1;
    GLint RendererState::program_text_u_blur = -1;
    GLint RendererState::program_text_u_invert = -1;
    GLint RendererState::program_text_a_pos = -1;
    GLint RendererState::program_text_a_uv = -1;
    GLint RendererState::program_text_a_color = -1;
    GLuint RendererState::vbo_text = 0;

    GLuint RendererState::program_image = 0;
    GLint RendererState::program_image_u_proj = -1;
    GLint RendererState::program_image_u_model = -1;
    GLint RendererState::program_image_u_tex = -1;
    GLint RendererState::program_image_u_radius = -1;
    GLint RendererState::program_image_u_size = -1;
    GLint RendererState::program_image_u_shadow_offset = -1;
    GLint RendererState::program_image_u_shadow_blur = -1;
    GLint RendererState::program_image_u_shadow_color = -1;
    GLint RendererState::program_image_a_pos = -1;
    GLint RendererState::program_image_a_uv = -1;

    void RendererState::update_proj()
    {
        float l = 0.0f;
        float r = static_cast<float>(viewport_width);
        float t = 0.0f;
        float b = static_cast<float>(viewport_height);

        float m00 = 2.0f / (r - l);
        float m11 = 2.0f / (t - b);
        float m30 = -(r + l) / (r - l);
        float m31 = -(t + b) / (t - b);

        proj = {
            m00, 0.0f, 0.0f, 0.0f,
            0.0f, m11, 0.0f, 0.0f,
            0.0f, 0.0f, -1.0f, 0.0f,
            m30, m31, 0.0f, 1.0f};
    }

    void Renderer::init()
    {
        // 1. Load Fonts
        FontManager::load_fonts();

        GLuint vs_color = compile_shader(GL_VERTEX_SHADER, vs_color_src);
        GLuint fs_color = compile_shader(GL_FRAGMENT_SHADER, fs_color_src);
        if (vs_color && fs_color)
            RendererState::program_color = link_program(vs_color, fs_color);
        if (RendererState::program_color)
        {
            RendererState::program_color_u_proj = glGetUniformLocation(RendererState::program_color, "uProj");
            RendererState::program_color_u_model = glGetUniformLocation(RendererState::program_color, "uModel");
            RendererState::program_color_u_size = glGetUniformLocation(RendererState::program_color, "uSize");
            RendererState::program_color_u_radius = glGetUniformLocation(RendererState::program_color, "uRadius");
            RendererState::program_color_u_blur = glGetUniformLocation(RendererState::program_color, "uBlur");
            RendererState::program_color_u_invert = glGetUniformLocation(RendererState::program_color, "uInvert");
            RendererState::program_color_u_grad_type = glGetUniformLocation(RendererState::program_color, "uGradType");
            RendererState::program_color_u_grad_c1 = glGetUniformLocation(RendererState::program_color, "uGradC1");
            RendererState::program_color_u_grad_c2 = glGetUniformLocation(RendererState::program_color, "uGradC2");
            RendererState::program_color_u_grad_c3 = glGetUniformLocation(RendererState::program_color, "uGradC3");
            RendererState::program_color_u_grad_c4 = glGetUniformLocation(RendererState::program_color, "uGradC4");
            RendererState::program_color_u_grad_s1 = glGetUniformLocation(RendererState::program_color, "uGradS1");
            RendererState::program_color_u_grad_s2 = glGetUniformLocation(RendererState::program_color, "uGradS2");
            RendererState::program_color_u_grad_s3 = glGetUniformLocation(RendererState::program_color, "uGradS3");
            RendererState::program_color_u_grad_s4 = glGetUniformLocation(RendererState::program_color, "uGradS4");
            RendererState::program_color_u_grad_count = glGetUniformLocation(RendererState::program_color, "uGradCount");
            RendererState::program_color_u_grad_angle = glGetUniformLocation(RendererState::program_color, "uGradAngle");
            RendererState::program_color_u_border_width = glGetUniformLocation(RendererState::program_color, "uBorderWidth");
            RendererState::program_color_a_pos = glGetAttribLocation(RendererState::program_color, "aPos");
            RendererState::program_color_a_color = glGetAttribLocation(RendererState::program_color, "aColor");
            RendererState::program_color_a_uv = glGetAttribLocation(RendererState::program_color, "aUV");
        }
        if (vs_color)
            glDeleteShader(vs_color);
        if (fs_color)
            glDeleteShader(fs_color);

        GLuint vs_text = compile_shader(GL_VERTEX_SHADER, vs_text_src);
        GLuint fs_text = compile_shader(GL_FRAGMENT_SHADER, fs_text_src);
        if (vs_text && fs_text)
            RendererState::program_text = link_program(vs_text, fs_text);
        if (RendererState::program_text)
        {
            RendererState::program_text_u_proj = glGetUniformLocation(RendererState::program_text, "uProj");
            RendererState::program_text_u_model = glGetUniformLocation(RendererState::program_text, "uModel");
            RendererState::program_text_u_tex = glGetUniformLocation(RendererState::program_text, "uTex");
            RendererState::program_text_u_blur = glGetUniformLocation(RendererState::program_text, "uBlur");
            RendererState::program_text_u_invert = glGetUniformLocation(RendererState::program_text, "uInvert");
            RendererState::program_text_a_pos = glGetAttribLocation(RendererState::program_text, "aPos");
            RendererState::program_text_a_uv = glGetAttribLocation(RendererState::program_text, "aUV");
            RendererState::program_text_a_color = glGetAttribLocation(RendererState::program_text, "aColor");
        }
        if (vs_text)
            glDeleteShader(vs_text);
        if (fs_text)
            glDeleteShader(fs_text);

        GLuint vs_image = compile_shader(GL_VERTEX_SHADER, vs_image_src);
        GLuint fs_image = compile_shader(GL_FRAGMENT_SHADER, fs_image_src);
        if (vs_image && fs_image)
            RendererState::program_image = link_program(vs_image, fs_image);
        if (RendererState::program_image)
        {
            RendererState::program_image_u_proj = glGetUniformLocation(RendererState::program_image, "uProj");
            RendererState::program_image_u_model = glGetUniformLocation(RendererState::program_image, "uModel");
            RendererState::program_image_u_tex = glGetUniformLocation(RendererState::program_image, "uTex");
            RendererState::program_image_u_radius = glGetUniformLocation(RendererState::program_image, "uRadius");
            RendererState::program_image_u_size = glGetUniformLocation(RendererState::program_image, "uSize");
            RendererState::program_image_u_shadow_offset = glGetUniformLocation(RendererState::program_image, "uShadowOffset");
            RendererState::program_image_u_shadow_blur = glGetUniformLocation(RendererState::program_image, "uShadowBlur");
            RendererState::program_image_u_shadow_color = glGetUniformLocation(RendererState::program_image, "uShadowColor");
            RendererState::program_image_a_pos = glGetAttribLocation(RendererState::program_image, "aPos");
            RendererState::program_image_a_uv = glGetAttribLocation(RendererState::program_image, "aUV");
        }
        if (vs_image)
            glDeleteShader(vs_image);
        if (fs_image)
            glDeleteShader(fs_image);

        glGenBuffers(1, &RendererState::vbo_color);
        glGenBuffers(1, &RendererState::vbo_text);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        RendererState::update_proj();
        Renderer::set_viewport(RendererState::viewport_width, RendererState::viewport_height);
    }

    void Renderer::set_viewport(int width, int height)
    {
        RendererState::viewport_width = std::max(1, width);
        RendererState::viewport_height = std::max(1, height);
        glViewport(0, 0, RendererState::viewport_width, RendererState::viewport_height);
        RendererState::update_proj();

        if (RendererState::program_color && RendererState::program_color_u_proj >= 0)
        {
            glUseProgram(RendererState::program_color);
            glUniformMatrix4fv(RendererState::program_color_u_proj, 1, GL_FALSE, RendererState::proj.data());
        }
        if (RendererState::program_text && RendererState::program_text_u_proj >= 0)
        {
            glUseProgram(RendererState::program_text);
            glUniformMatrix4fv(RendererState::program_text_u_proj, 1, GL_FALSE, RendererState::proj.data());
            if (RendererState::program_text_u_tex >= 0)
                glUniform1i(RendererState::program_text_u_tex, 0);
        }
        if (RendererState::program_image && RendererState::program_image_u_proj >= 0)
        {
            glUseProgram(RendererState::program_image);
            glUniformMatrix4fv(RendererState::program_image_u_proj, 1, GL_FALSE, RendererState::proj.data());
            if (RendererState::program_image_u_tex >= 0)
                glUniform1i(RendererState::program_image_u_tex, 0);
        }
        glUseProgram(0);
    }

    int Renderer::get_viewport_width()
    {
        return RendererState::viewport_width;
    }

    int Renderer::get_viewport_height()
    {
        return RendererState::viewport_height;
    }

    void Renderer::set_debug_mode(DebugType type)
    {
        debug_type = type;
    }

    void Renderer::set_debug_box(const std::shared_ptr<LayoutBox>& box)
    {
        debug_box = box;
    }

} // namespace linweb
