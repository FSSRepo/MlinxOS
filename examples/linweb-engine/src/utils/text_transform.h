#pragma once
#include <cctype>
#include <cstdlib>
#include <string>

namespace linweb {

// CSS letter-spacing in px ("normal" and empty mean 0).
inline float parse_letter_spacing(const std::string& value) {
    if (value.empty() || value == "normal") return 0.0f;
    return static_cast<float>(std::strtod(value.c_str(), nullptr));
}

// Number of characters (UTF-8 code points) in a string.
inline size_t utf8_char_count(const std::string& text) {
    size_t n = 0;
    for (unsigned char c : text) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

// CSS text-transform for a text run: uppercase, lowercase or capitalize. Other values (and none) keep the text.
// Only ASCII letters change; multi-byte UTF-8 sequences are left as they are.
inline std::string apply_text_transform(const std::string& text, const std::string& mode) {
    if (mode == "uppercase") {
        std::string out = text;
        for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return out;
    }
    if (mode == "lowercase") {
        std::string out = text;
        for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }
    if (mode == "capitalize") {
        std::string out = text;
        bool word_start = true;
        for (char& c : out) {
            unsigned char u = static_cast<unsigned char>(c);
            if (std::isspace(u)) word_start = true;
            else {
                if (word_start) c = static_cast<char>(std::toupper(u));
                word_start = false;
            }
        }
        return out;
    }
    return text;
}

} // namespace linweb
