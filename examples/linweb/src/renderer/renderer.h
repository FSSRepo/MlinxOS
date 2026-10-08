#pragma once
#include "layout/layout_engine.h"
#include "gl_wrapper.h"

namespace linweb {

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
    float angle = 0; // For linear: degrees. For radial: not used yet.
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
    static float measure_text(const std::string& text, float font_size = 16.0f, bool is_bold = false, bool is_monospace = false);
    static float get_line_height(float font_size = 16.0f);
    static void render(const std::shared_ptr<LayoutBox>& root);
    static void set_debug_mode(DebugType type);
    static void set_debug_box(const std::shared_ptr<LayoutBox>& box);
    static void draw_rect(float x, float y, float width, float height, float r, float g, float b, float a, float radius = 0.0f, float blur = 0.0f, float invert = 0.0f, const Gradient& gradient = Gradient(), float border_width = 0.0f);
    static void draw_image(GLuint texture_id, float x, float y, float width, float height, float radius = 0.0f,
                           float shadow_r = 0.0f, float shadow_g = 0.0f, float shadow_b = 0.0f, float shadow_a = 0.0f,
                           float shadow_offset_x = 0.0f, float shadow_offset_y = 0.0f, float shadow_blur = 0.0f,
                           int tex_width = 0, int tex_height = 0);
    static void draw_text(const std::string& text, float x, float y, float font_size = 16.0f, float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 1.0f, bool is_bold = false, float blur = 0.0f, float invert = 0.0f, bool is_monospace = false);

private:
    static void render_box(const std::shared_ptr<LayoutBox>& box, float inherited_opacity = 1.0f, const Filter& inherited_filter = Filter());

    static DebugType debug_type;
    static std::shared_ptr<LayoutBox> debug_box;
};

} // namespace linweb
