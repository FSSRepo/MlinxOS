#pragma once
#include "layout/layout_engine.h"
#include "gl_wrapper.h"
#include <functional>
#include <string>

namespace linweb {

struct CustomFont;

// An animated background drawn by a fragment shader, named in CSS (`background: <name>`). The engine registers one of its own
// (ps3-xmb); a module registers more with Renderer::register_shader_background before the page is drawn.
// The vertex shader is the engine's (attributes aPos and aUV, uniforms uProj and uModel, varying vUV).
struct ShaderBackgroundSpec {
    std::string name;                    // the value of the CSS `background`
    std::string fragment_gles;           // fragment shader for GLES2 (the Weston build)
    std::string fragment_gl;             // fragment shader for desktop GL 1.10 (#version 110)
    bool animated = true;                // true: the screen is redrawn on every frame while the background is visible
    // Called after glUseProgram(program) for each box that has the background: set the uniforms here. x, y, width, height are the
    // box in pixels, opacity is the element opacity (0..1, multiply it into the alpha the shader writes), time is in seconds.
    std::function<void(unsigned program, float x, float y, float width, float height, float opacity, double time)> set_uniforms;
};

enum class GradientType {
    None,
    Linear,
    Radial
};

struct Gradient {
    GradientType type = GradientType::None;
    float r1 = 0, g1 = 0, b1 = 0, a1 = 0;
    float r2 = 0, g2 = 0, b2 = 0, a2 = 0;
    float r3 = 0, g3 = 0, b3 = 0, a3 = 0;
    float r4 = 0, g4 = 0, b4 = 0, a4 = 0;
    float s1 = 0.0f, s2 = 1.0f, s3 = 0.0f, s4 = 0.0f;
    int color_count = 2;
    float angle = 180; // CSS linear gradients default to top-to-bottom.
};

struct BoxShadow {
    float offsetX = 0;
    float offsetY = 0;
    float blur = 0;
    float spread = 0;
    float r = 0, g = 0, b = 0, a = 0;
    bool active = false;
};

struct Filter {
    float blur = 0;
    float invert = 0;
    BoxShadow dropShadow;
    bool hasDropShadow = false;
};

enum class DebugType {
    None,
    Element,
    Text,
    Dimens,
    Anim
};

class Renderer {
public:
    static void init();
    static void set_viewport(int width, int height);
    static int get_viewport_width();
    static int get_viewport_height();
    // custom selects a loaded @font-face face; it replaces the bold and monospace choice when given
    static float measure_text(const std::string& text, float font_size = 16.0f, bool is_bold = false, bool is_monospace = false, const CustomFont* custom = nullptr);
    static float font_ascent(const CustomFont* custom = nullptr);
    static float get_line_height(float font_size = 16.0f, const CustomFont* custom = nullptr);
    static float scaled_ascent(float font_size, const CustomFont* custom = nullptr);
    static void render(const std::shared_ptr<LayoutBox>& root);
    // The video session (media/video_player.h) a <video> element shows; an element without one shows nothing. A session
    // number of 0 or less takes the element out.
    static void bind_video(const void* node, int session);
    static void set_debug_mode(DebugType type);
    static void set_debug_box(const std::shared_ptr<LayoutBox>& box);
    // Makes `background: <spec.name>` draw with the shader of the spec; a second registration of a name replaces the first.
    static void register_shader_background(ShaderBackgroundSpec spec);
    // True when the last frame drew an animated shader background: the screen has to be drawn again for the next one.
    static bool animated_background_visible();
    static void draw_rect(float x, float y, float width, float height, float r, float g, float b, float a, float radius = 0.0f, float blur = 0.0f, float invert = 0.0f, const Gradient& gradient = Gradient(), float border_width = 0.0f, bool is_shadow = false);
    static void draw_image(GLuint texture_id, float x, float y, float width, float height, float radius = 0.0f,
                           float shadow_r = 0.0f, float shadow_g = 0.0f, float shadow_b = 0.0f, float shadow_a = 0.0f,
                           float shadow_offset_x = 0.0f, float shadow_offset_y = 0.0f, float shadow_blur = 0.0f,
                           int tex_width = 0, int tex_height = 0, float opacity = 1.0f);
    static void draw_text(const std::string& text, float x, float y, float font_size = 16.0f, float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 1.0f, bool is_bold = false, float blur = 0.0f, float invert = 0.0f, bool is_monospace = false, const CustomFont* custom = nullptr);

private:
    static void render_box(const std::shared_ptr<LayoutBox>& box, float inherited_opacity = 1.0f, const Filter& inherited_filter = Filter());
    static void draw_shader_background(const std::string& name, float x, float y, float width, float height, float opacity);

    static DebugType debug_type;
    static std::shared_ptr<LayoutBox> debug_box;
};

} // namespace linweb
