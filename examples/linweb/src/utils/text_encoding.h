#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace linweb {
inline void append_utf8(std::string& out, uint32_t c) {
    if (c == 0 || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) c = 0xfffd;
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) { out += static_cast<char>(0xc0 | c >> 6); out += static_cast<char>(0x80 | (c & 63)); }
    else if (c < 0x10000) {
        out += static_cast<char>(0xe0 | c >> 12); out += static_cast<char>(0x80 | ((c >> 6) & 63)); out += static_cast<char>(0x80 | (c & 63));
    } else {
        out += static_cast<char>(0xf0 | c >> 18); out += static_cast<char>(0x80 | ((c >> 12) & 63));
        out += static_cast<char>(0x80 | ((c >> 6) & 63)); out += static_cast<char>(0x80 | (c & 63));
    }
}
// Decode UTF-8; isolated high bytes are treated as Latin-1 for legacy HTML.
inline uint32_t next_codepoint(const std::string& text, size_t& pos) {
    unsigned char first = static_cast<unsigned char>(text[pos++]);
    if (first < 0x80) return first;
    unsigned count = first >= 0xc2 && first <= 0xdf ? 1 : first >= 0xe0 && first <= 0xef ? 2 : first >= 0xf0 && first <= 0xf4 ? 3 : 0;
    if (!count || pos + count > text.size()) return first;
    uint32_t c = first & (0x7f >> (count + 1));
    for (unsigned i = 0; i < count; ++i) {
        unsigned char byte = static_cast<unsigned char>(text[pos + i]);
        if ((byte & 0xc0) != 0x80) return first;
        c = c << 6 | (byte & 63);
    }
    if ((count == 1 && c < 0x80) || (count == 2 && c < 0x800) || (count == 3 && c < 0x10000) ||
        c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return first;
    pos += count;
    return c;
}
inline std::string decode_html_entities(const std::string& text) {
    static const std::map<std::string, uint32_t> named = {
        {"amp",38},{"lt",60},{"gt",62},{"quot",34},{"apos",39},{"nbsp",160},{"copy",169},{"reg",174},
        {"aacute",225},{"eacute",233},{"iacute",237},{"oacute",243},{"uacute",250},{"ntilde",241},
        {"Aacute",193},{"Eacute",201},{"Iacute",205},{"Oacute",211},{"Uacute",218},{"Ntilde",209},
        {"uuml",252},{"iexcl",161},{"iquest",191},{"ndash",0x2013},{"mdash",0x2014},{"hellip",0x2026}
    };
    std::string result;
    for (size_t pos = 0; pos < text.size();) {
        if (text[pos] == '&') {
            size_t end = text.find(';', pos + 1);
            if (end != std::string::npos && end - pos <= 32) {
                std::string entity = text.substr(pos + 1, end - pos - 1);
                auto found = named.find(entity);
                if (found != named.end()) { append_utf8(result, found->second); pos = end + 1; continue; }
                if (!entity.empty() && entity[0] == '#') {
                    try {
                        bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
                        std::string digits = entity.substr(hex ? 2 : 1);
                        size_t consumed = 0;
                        unsigned long code = std::stoul(digits, &consumed, hex ? 16 : 10);
                        if (consumed == digits.size()) { append_utf8(result, code); pos = end + 1; continue; }
                    } catch (...) {}
                }
            }
        }
        result += text[pos++];
    }
    return result;
}
}
