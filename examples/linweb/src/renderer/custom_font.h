#pragma once
#include <string>
#include <vector>
#include "gl_wrapper.h"
#include "stb_truetype.h"

namespace linweb {

// A font loaded from an @font-face rule. Its glyphs are baked at the base size, like the built-in faces.
struct CustomFont {
    std::string family;   // lowercase, without quotes
    int weight = 400;
    bool italic = false;
    std::vector<unsigned char> data;
    stbtt_bakedchar glyphs[224];
    GLuint texture = 0;
    float ascent_px = 0;
    float descent_px = 0;   // below the baseline, positive
    float gap_px = 0;       // extra leading between lines
    float line_height_px = 0;
};

class CustomFonts {
public:
    // Loads the file of a face and uploads its atlas. Needs a GL context.
    static bool add(const std::string& family, const std::string& font_weight, bool italic, const std::string& path);

    // The face a text run uses: the first family of font-family that has faces, at the nearest weight.
    // Null when none of the families is loaded, so the built-in faces are used.
    static const CustomFont* select(const std::string& font_family, const std::string& font_weight, const std::string& font_style);
};

} // namespace linweb
