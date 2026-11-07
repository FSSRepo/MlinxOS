#pragma once
#include <string>
#include <algorithm>
#include <cstdio>

namespace linweb {

struct Color {
    float r, g, b, a;

    static Color from_rgba(float r, float g, float b, float a = 1.0f) {
        return {r, g, b, a};
    }
};

static inline int hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static inline bool parse_hex_color(const std::string& s, Color& out) {
    if (s.empty() || s[0] != '#') return false;
    
    if (s.size() == 4) {
        int r = hex_to_int(s[1]);
        int g = hex_to_int(s[2]);
        int b = hex_to_int(s[3]);
        if (r < 0 || g < 0 || b < 0) return false;
        out.r = ((r << 4) | r) / 255.0f;
        out.g = ((g << 4) | g) / 255.0f;
        out.b = ((b << 4) | b) / 255.0f;
        out.a = 1.0f;
        return true;
    }

    if (s.size() != 7 && s.size() != 9) return false;

    int r0 = hex_to_int(s[1]), r1 = hex_to_int(s[2]);
    int g0 = hex_to_int(s[3]), g1 = hex_to_int(s[4]);
    int b0 = hex_to_int(s[5]), b1 = hex_to_int(s[6]);
    
    if (r0 < 0 || r1 < 0 || g0 < 0 || g1 < 0 || b0 < 0 || b1 < 0) return false;
    
    out.r = ((r0 << 4) | r1) / 255.0f;
    out.g = ((g0 << 4) | g1) / 255.0f;
    out.b = ((b0 << 4) | b1) / 255.0f;

    if (s.size() == 9) {
        int a0 = hex_to_int(s[7]), a1 = hex_to_int(s[8]);
        if (a0 < 0 || a1 < 0) return false;
        out.a = ((a0 << 4) | a1) / 255.0f;
    } else {
        out.a = 1.0f;
    }
    return true;
}

static inline std::string color_to_str(Color c) {
    auto to_hex = [](float f) {
        int i = std::max(0, std::min(255, (int)(f * 255.0f)));
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", i);
        return std::string(buf);
    };
    return "#" + to_hex(c.r) + to_hex(c.g) + to_hex(c.b) + to_hex(c.a);
}

static inline std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return s.substr(start, end - start);
}

static inline Color parse_color_str(const std::string& s) {
    std::string name = trim(s);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { 
        return std::tolower(c); 
    });

    if (name == "transparent") return {0,0,0,0};
    if (name == "white") return {1,1,1,1};
    if (name == "black") return {0,0,0,1};
    if (name == "red") return {1,0,0,1};
    if (name == "green") return {0,0.5f,0,1};
    if (name == "blue") return {0,0,1,1};
    if (name == "yellow") return {1,1,0,1};
    if (name == "cyan") return {0,1,1,1};
    if (name == "magenta") return {1,0,1,1};
    if (name == "gray" || name == "grey") return {0.5f,0.5f,0.5f,1};
    if (name == "silver") return {0.75f,0.75f,0.75f,1};
    if (name == "maroon") return {0.5f,0,0,1};
    if (name == "olive") return {0.5f,0.5f,0,1};
    if (name == "lime") return {0,1,0,1};
    if (name == "teal") return {0,0.5f,0.5f,1};
    if (name == "navy") return {0,0,0.5f,1};
    if (name == "purple") return {0.5f,0,0.5f,1};
    if (name == "orange") return {1,0.5f,0,1};
    if (name == "brown") return {0.65f,0.16f,0.16f,1};

    Color c = {0,0,0,1};
    if (parse_hex_color(name, c)) return c;

    if (name.substr(0, 4) == "rgba" || name.substr(0, 3) == "rgb") {
        bool has_alpha = name.substr(0, 4) == "rgba";
        std::string content = name.substr(has_alpha ? 5 : 4);
        if (!content.empty() && content.back() == ')') content.pop_back();
        
        float r = 0, g = 0, b = 0, a = 1.0f;
        if (has_alpha) {
            sscanf(content.c_str(), "%f , %f , %f , %f", &r, &g, &b, &a);
        } else {
            sscanf(content.c_str(), "%f , %f , %f", &r, &g, &b);
        }
        return {r / 255.0f, g / 255.0f, b / 255.0f, a};
    }

    return {0,0,0,0};
}

} // namespace linweb
