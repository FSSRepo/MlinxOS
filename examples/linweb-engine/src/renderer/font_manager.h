#pragma once
#include <string>
#include "stb_truetype.h"

namespace linweb {

class FontManager {
public:
    static bool load_fonts();
    static bool load_font(const std::string& path, bool bold = false);
    static bool load_monospace_font(const std::string& path);
    static stbtt_fontinfo* get_font(bool bold = false);
    static stbtt_bakedchar* get_cached_glyph(char c, bool bold = false);
    static void cache_glyph(char c, bool bold = false);
};

} // namespace linweb
