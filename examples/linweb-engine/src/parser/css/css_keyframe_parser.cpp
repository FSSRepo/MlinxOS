#include "parser/css/css_keyframe_parser.h"

namespace linweb {

KeyframesRule parse_keyframes(CSSTokenizer& t) {
    KeyframesRule kr;
    t.pos += 10; // skip "@keyframes"
    t.consume_whitespace();
    kr.name = t.parse_identifier();
    t.consume_whitespace();
    if (t.pos < t.input.size() && t.input[t.pos] == '{') {
        t.consume_char(); // {
        kr.keyframes = parse_keyframe_list(t);
        t.consume_whitespace();
        if (t.pos < t.input.size() && t.input[t.pos] == '}') t.consume_char(); // }
    }
    return kr;
}

std::vector<Keyframe> parse_keyframe_list(CSSTokenizer& t) {
    std::vector<Keyframe> keyframes;
    while (true) {
        t.consume_whitespace();
        if (t.pos >= t.input.size()) break;
        if (t.input[t.pos] == '}') break;
        auto new_kfs = parse_keyframe(t);
        keyframes.insert(keyframes.end(), new_kfs.begin(), new_kfs.end());
    }
    return keyframes;
}

std::vector<Keyframe> parse_keyframe(CSSTokenizer& t) {
    std::vector<std::string> offsets;
    while (true) {
        t.consume_whitespace();
        std::string off = t.consume_while([](char c) { return c != '{' && !std::isspace(static_cast<unsigned char>(c)) && c != ','; });
        if (!off.empty()) offsets.push_back(off);
        t.consume_whitespace();
        if (t.pos < t.input.size() && t.input[t.pos] == ',') {
            t.consume_char(); // skip comma
            continue;
        }
        break;
    }

    std::vector<Declaration> declarations;
    t.consume_whitespace();
    if (t.pos < t.input.size() && t.input[t.pos] == '{') {
        t.consume_char(); // {
        declarations = parse_declarations(t);
        t.consume_whitespace();
        if (t.pos < t.input.size() && t.input[t.pos] == '}') t.consume_char(); // }
    }

    std::vector<Keyframe> result;
    for (const auto& off : offsets) {
        Keyframe k;
        k.offset = off;
        k.declarations = declarations;
        result.push_back(k);
    }
    return result;
}

} // namespace linweb
