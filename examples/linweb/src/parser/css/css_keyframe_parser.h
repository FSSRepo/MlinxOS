#pragma once
#include "parser/css/css_tokenizer.h"
#include "parser/css/css_declaration_parser.h"
#include <string>
#include <vector>

namespace linweb {

struct Keyframe {
    std::string offset;
    std::vector<Declaration> declarations;
};

struct KeyframesRule {
    std::string name;
    std::vector<Keyframe> keyframes;
};

KeyframesRule parse_keyframes(CSSTokenizer& t);
std::vector<Keyframe> parse_keyframe(CSSTokenizer& t);
std::vector<Keyframe> parse_keyframe_list(CSSTokenizer& t);

} // namespace linweb
