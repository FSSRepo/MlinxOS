#pragma once
#include "parser/css/css_tokenizer.h"
#include "parser/css/css_selector_parser.h"
#include "parser/css/css_declaration_parser.h"
#include "parser/css/css_keyframe_parser.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace linweb {

struct Rule {
    std::vector<Selector> selectors;
    std::vector<Declaration> declarations;
};

// An @font-face rule: the family it names, the file it loads, and the weight and style it is used for
struct FontFaceRule {
    std::string family;
    std::string source;
    std::string weight;
    std::string style;
};

struct StyleSheet {
    std::vector<Rule> rules;
    std::unordered_map<std::string, KeyframesRule> keyframes;
    std::vector<FontFaceRule> font_faces;
    // which pseudo-elements the rules style, so the style pass only looks for them when a rule names them
    bool has_first_line_rules = false;
    bool has_scrollbar_rules = false;
};

class CSSParser {
public:
    static StyleSheet parse(const std::string& source, float viewport_width = 800, float viewport_height = 600);
    static std::vector<Selector> parse_selectors(const std::string& source);

private:
    CSSParser(std::string source);
    
    std::vector<Rule> parse_rules(StyleSheet& sheet);
    Rule parse_rule();

    CSSTokenizer tokenizer;
    float viewport_width_ = 800, viewport_height_ = 600;
};

} // namespace linweb
