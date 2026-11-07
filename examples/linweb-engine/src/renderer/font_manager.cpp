#include "renderer/renderer_internal.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "renderer/font_manager.h"
#include "utils/file_utils.h"
#include "utils/memory_files.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstring>

namespace linweb {

bool FontManager::load_fonts() {
    std::string exe_dir = get_executable_dir();
    if (!load_font(exe_dir + "/arial.ttf", false)) {
        return false;
    }
    if (!load_font(exe_dir + "/arialbd.ttf", true)) {
        std::cerr << "WARNING: Could not load bold font '" << exe_dir << "/arialbd.ttf'. Falling back to regular font." << std::endl;
        RendererState::ftex_bold = RendererState::ftex;
        RendererState::font_bold_info = RendererState::font_info;
        memcpy(RendererState::cdata_bold, RendererState::cdata, sizeof(RendererState::cdata));
    }
    if (!load_monospace_font(exe_dir + "/monospace.ttf")) {
        std::cerr << "WARNING: Could not load monospace font '" << exe_dir << "/monospace.ttf'. Monospace elements will use regular font." << std::endl;
        RendererState::ftex_mono = RendererState::ftex;
        RendererState::font_mono_info = RendererState::font_info;
        memcpy(RendererState::cdata_mono, RendererState::cdata, sizeof(RendererState::cdata));
    }
    return true;
}

bool FontManager::load_monospace_font(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    const auto memory = memory_files::find(path);
    if (!file && !memory) {
        return false;
    }

    std::streamsize loaded = 0;
    if (memory) {
        loaded = static_cast<std::streamsize>(std::min<size_t>(memory->size(), 1 << 20));
        memcpy(RendererState::font_mono_buffer, memory->data(), static_cast<size_t>(loaded));
    } else {
        file.read((char*)RendererState::font_mono_buffer, 1 << 20);
        loaded = file.gcount();
    }
    std::cout << "Monospace font loaded: " << loaded << " bytes." << std::endl;

    auto temp_bitmap = std::make_unique<unsigned char[]>(1024 * 1024);
    stbtt_BakeFontBitmap(RendererState::font_mono_buffer, 0, RendererState::base_font_size, temp_bitmap.get(), 1024, 1024, 32, 224, RendererState::cdata_mono);

    glGenTextures(1, &RendererState::ftex_mono);
    glBindTexture(GL_TEXTURE_2D, RendererState::ftex_mono);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 1024, 1024, 0, GL_ALPHA, GL_UNSIGNED_BYTE, temp_bitmap.get());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbtt_InitFont(&RendererState::font_mono_info, RendererState::font_mono_buffer, stbtt_GetFontOffsetForIndex(RendererState::font_mono_buffer, 0));

    return true;
}

bool FontManager::load_font(const std::string& path, bool bold) {
    std::ifstream file(path, std::ios::binary);
    const auto memory = memory_files::find(path);
    if (!file && !memory) {
        if (!bold) {
            std::cerr << "CRITICAL: Failed to load font at '" << path << "'. Please check if the file exists." << std::endl;
        }
        return false;
    }

    unsigned char* buffer = bold ? RendererState::font_bold_buffer : RendererState::font_buffer;
    stbtt_bakedchar* cdata = bold ? RendererState::cdata_bold : RendererState::cdata;
    GLuint& ftex = bold ? RendererState::ftex_bold : RendererState::ftex;
    stbtt_fontinfo& font_info = bold ? RendererState::font_bold_info : RendererState::font_info;

    std::streamsize loaded = 0;
    if (memory) {
        loaded = static_cast<std::streamsize>(std::min<size_t>(memory->size(), 1 << 20));
        memcpy(buffer, memory->data(), static_cast<size_t>(loaded));
    } else {
        file.read((char*)buffer, 1 << 20);
        loaded = file.gcount();
    }
    std::cout << (bold ? "Bold" : "Regular") << " font loaded: " << loaded << " bytes." << std::endl;

    // stbtt bakes a pixel height spread over ascent - descent, not over the em. Bake at the height
    // that makes one em equal base_font_size; otherwise every advance comes out ~10% narrow (Arial: 2288/2048).
    stbtt_fontinfo bake_info;
    stbtt_InitFont(&bake_info, buffer, stbtt_GetFontOffsetForIndex(buffer, 0));
    int bake_ascent = 0, bake_descent = 0, bake_gap = 0;
    stbtt_GetFontVMetrics(&bake_info, &bake_ascent, &bake_descent, &bake_gap);
    float bake_height = RendererState::base_font_size * (float)(bake_ascent - bake_descent) * stbtt_ScaleForMappingEmToPixels(&bake_info, 1.0f);

    auto temp_bitmap = std::make_unique<unsigned char[]>(1024 * 1024);
    stbtt_BakeFontBitmap(buffer, 0, bake_height, temp_bitmap.get(), 1024, 1024, 32, 224, cdata);

    glGenTextures(1, &ftex);
    glBindTexture(GL_TEXTURE_2D, ftex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 1024, 1024, 0, GL_ALPHA, GL_UNSIGNED_BYTE, temp_bitmap.get());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbtt_InitFont(&font_info, buffer, stbtt_GetFontOffsetForIndex(buffer, 0));

    if (!bold) {
        int ascent = 0, descent = 0, line_gap = 0;
        stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);
        // metrics are per em, like the glyph bitmaps baked above; ScaleForPixelHeight would use ascent - descent
        float scale = stbtt_ScaleForMappingEmToPixels(&font_info, RendererState::base_font_size);
        RendererState::font_ascent_px = (float)ascent * scale;
        RendererState::font_descent_px = (float)(-descent) * scale;
        RendererState::font_gap_px = (float)line_gap * scale;
        RendererState::font_line_height_px = (float)(ascent - descent + line_gap) * scale;
        if (!(RendererState::font_line_height_px > 0.0f))
            RendererState::font_line_height_px = 24.0f;
    }

    return true;
}

stbtt_fontinfo* FontManager::get_font(bool bold) {
    return bold ? &RendererState::font_bold_info : &RendererState::font_info;
}

stbtt_bakedchar* FontManager::get_cached_glyph(char c, bool bold) {
    if (c < 32 || c >= 128) return nullptr;
    stbtt_bakedchar* cdata = bold ? RendererState::cdata_bold : RendererState::cdata;
    return &cdata[c - 32];
}

void FontManager::cache_glyph(char c, bool bold) {
    (void)c;
    (void)bold;
}

} // namespace linweb
