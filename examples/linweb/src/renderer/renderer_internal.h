#pragma once
#include "renderer/renderer.h"
#include "gl_wrapper.h"
#include <array>
#include <string>
#include <vector>
#include "stb_truetype.h"

namespace linweb {

struct RendererState {
    static unsigned char font_buffer[1 << 20];
    static unsigned char font_bold_buffer[1 << 20];
    static unsigned char font_mono_buffer[1 << 20];
    static stbtt_bakedchar cdata[96];
    static stbtt_bakedchar cdata_bold[96];
    static stbtt_bakedchar cdata_mono[96];
    static GLuint ftex;
    static GLuint ftex_bold;
    static GLuint ftex_mono;
    static stbtt_fontinfo font_info;
    static stbtt_fontinfo font_bold_info;
    static stbtt_fontinfo font_mono_info;
    static float base_font_size;
    static float font_ascent_px;
    static float font_line_height_px;

    static int viewport_width;
    static int viewport_height;
    static std::array<float, 16> proj;
    static std::array<float, 16> current_model;

    static GLuint program_color;
    static GLint program_color_u_proj;
    static GLint program_color_u_model;
    static GLint program_color_u_size;
    static GLint program_color_u_radius;
    static GLint program_color_u_blur;
    static GLint program_color_u_invert;
    static GLint program_color_u_grad_type;
    static GLint program_color_u_grad_c1;
    static GLint program_color_u_grad_c2;
    static GLint program_color_u_grad_c3;
    static GLint program_color_u_grad_c4;
    static GLint program_color_u_grad_s1;
    static GLint program_color_u_grad_s2;
    static GLint program_color_u_grad_s3;
    static GLint program_color_u_grad_s4;
    static GLint program_color_u_grad_count;
    static GLint program_color_u_grad_angle;
    static GLint program_color_u_border_width;
    static GLint program_color_a_pos;
    static GLint program_color_a_color;
    static GLint program_color_a_uv;
    static GLuint vbo_color;

    static GLuint program_text;
    static GLint program_text_u_proj;
    static GLint program_text_u_model;
    static GLint program_text_u_tex;
    static GLint program_text_u_blur;
    static GLint program_text_u_invert;
    static GLint program_text_a_pos;
    static GLint program_text_a_uv;
    static GLint program_text_a_color;
    static GLuint vbo_text;

    static GLuint program_image;
    static GLint program_image_u_proj;
    static GLint program_image_u_model;
    static GLint program_image_u_tex;
    static GLint program_image_u_radius;
    static GLint program_image_u_size;
    static GLint program_image_u_shadow_offset;
    static GLint program_image_u_shadow_blur;
    static GLint program_image_u_shadow_color;
    static GLint program_image_a_pos;
    static GLint program_image_a_uv;

    static void update_proj();
};

// Helper functions
void parse_color(const std::string& value, float& r, float& g, float& b, float& a);
float parse_length(const std::string& value);
BoxShadow parse_box_shadow(const std::string& value);
Filter parse_filter(const std::string& value);
Gradient parse_gradient(const std::string& value);
GLuint compile_shader(GLenum type, const char* source);
GLuint link_program(GLuint vs, GLuint fs);

} // namespace linweb
