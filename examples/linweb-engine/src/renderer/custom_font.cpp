#include "renderer/custom_font.h"
#include "renderer/renderer_internal.h"
#include "utils/memory_files.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>

namespace linweb {

static std::vector<std::unique_ptr<CustomFont>>& faces() {
    static std::vector<std::unique_ptr<CustomFont>> loaded;
    return loaded;
}

// A family name as the lookup compares it: lowercase, without quotes or surrounding spaces
static std::string normalized_family(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c == '"' || c == '\'') continue;
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    size_t start = out.find_first_not_of(" \t");
    size_t end = out.find_last_not_of(" \t");
    return start == std::string::npos ? std::string() : out.substr(start, end - start + 1);
}

// The numeric weight a font-weight value asks for; 400 when it is not a weight this engine knows
static int requested_weight(const std::string& font_weight) {
    if (font_weight.empty() || font_weight == "normal") return 400;
    if (font_weight == "bold") return 700;
    if (font_weight == "lighter") return 300;
    if (font_weight == "bolder") return 700;
    try {
        return std::stoi(font_weight);
    } catch (...) {
        return 400;
    }
}

bool CustomFonts::add(const std::string& family, const std::string& font_weight, bool italic, const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    const auto memory = memory_files::find(path);
    if (!file && !memory) {
        std::cerr << "Could not load the font " << path << std::endl;
        return false;
    }
    auto font = std::make_unique<CustomFont>();
    font->family = normalized_family(family);
    font->weight = requested_weight(font_weight);
    font->italic = italic;
    if (memory) font->data.assign(memory->begin(), memory->end());
    else font->data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (font->data.empty() || stbtt_GetNumberOfFonts(font->data.data()) <= 0) {
        std::cerr << "Not a font file: " << path << std::endl;
        return false;
    }

    // stb takes a pixel height spread over ascent - descent; bake at the height that makes one em the base size
    stbtt_fontinfo info;
    stbtt_InitFont(&info, font->data.data(), stbtt_GetFontOffsetForIndex(font->data.data(), 0));
    int ascent = 0, descent = 0, line_gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
    float em_scale = stbtt_ScaleForMappingEmToPixels(&info, RendererState::base_font_size);
    float bake_height = RendererState::base_font_size * (float)(ascent - descent) * stbtt_ScaleForMappingEmToPixels(&info, 1.0f);
    font->ascent_px = (float)ascent * em_scale;
    font->descent_px = (float)(-descent) * em_scale;
    font->gap_px = (float)line_gap * em_scale;
    font->line_height_px = (float)(ascent - descent + line_gap) * em_scale;

    auto bitmap = std::make_unique<unsigned char[]>(1024 * 1024);
    stbtt_BakeFontBitmap(font->data.data(), 0, bake_height, bitmap.get(), 1024, 1024, 32, 224, font->glyphs);

    glGenTextures(1, &font->texture);
    glBindTexture(GL_TEXTURE_2D, font->texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 1024, 1024, 0, GL_ALPHA, GL_UNSIGNED_BYTE, bitmap.get());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    std::cout << "Font loaded: " << family << " " << font->weight << (italic ? " italic" : "") << " from " << path << std::endl;
    faces().push_back(std::move(font));
    return true;
}

const CustomFont* CustomFonts::select(const std::string& font_family, const std::string& font_weight, const std::string& font_style) {
    if (faces().empty()) return nullptr;
    int desired = requested_weight(font_weight);
    bool wants_italic = font_style == "italic" || font_style == "oblique";

    std::string list = font_family;
    size_t from = 0;
    while (from <= list.size()) {
        size_t comma = list.find(',', from);
        std::string name = normalized_family(list.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
        from = comma == std::string::npos ? list.size() + 1 : comma + 1;

        std::vector<const CustomFont*> candidates;
        for (const auto& face : faces()) {
            if (face->family == name) candidates.push_back(face.get());
        }
        if (candidates.empty()) continue;

        // a style is matched first; a family without the wanted style is used in its other style
        std::vector<const CustomFont*> by_style;
        for (const CustomFont* face : candidates) {
            if (face->italic == wants_italic) by_style.push_back(face);
        }
        if (!by_style.empty()) candidates = by_style;

        // the weight rule of CSS: below the wanted one first for a light request, above it first for a heavy one
        auto closest = [&](int target, bool below) -> const CustomFont* {
            const CustomFont* best = nullptr;
            for (const CustomFont* face : candidates) {
                bool in_range = below ? face->weight <= target : face->weight >= target;
                if (!in_range) continue;
                if (!best || (below ? face->weight > best->weight : face->weight < best->weight)) best = face;
            }
            return best;
        };
        // the lightest face whose weight lies in [from, to]
        auto closest_in = [&](int from_weight, int to_weight) -> const CustomFont* {
            const CustomFont* best = nullptr;
            for (const CustomFont* face : candidates) {
                if (face->weight < from_weight || face->weight > to_weight) continue;
                if (!best || face->weight < best->weight) best = face;
            }
            return best;
        };
        const CustomFont* chosen = nullptr;
        if (desired < 400) {
            chosen = closest(desired, true);
            if (!chosen) chosen = closest(desired, false);
        } else if (desired > 500) {
            chosen = closest(desired, false);
            if (!chosen) chosen = closest(desired, true);
        } else {
            // 400 to 500: the weights from the wanted one up to 500, then the lighter ones, then the heavier ones
            chosen = closest_in(desired, 500);
            if (!chosen) chosen = closest(desired, true);
            if (!chosen) chosen = closest(500, false);
        }
        if (chosen) return chosen;
    }
    return nullptr;
}

} // namespace linweb
