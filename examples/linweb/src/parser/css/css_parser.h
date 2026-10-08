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

struct StyleSheet {
    std::vector<Rule> rules;
    std::unordered_map<std::string, KeyframesRule> keyframes;
};

class CSSParser {
public:
    static StyleSheet parse(const std::string& source);
    static std::vector<Selector> parse_selectors(const std::string& source);

private:
    CSSParser(std::string source);
    
    std::vector<Rule> parse_rules(StyleSheet& sheet);
    Rule parse_rule();

    CSSTokenizer tokenizer;
};

} // namespace linweb
