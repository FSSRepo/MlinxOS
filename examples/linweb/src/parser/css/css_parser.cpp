#include "parser/css/css_parser.h"
#include "parser/css/css_selector_parser.h"
#include "parser/css/css_declaration_parser.h"
#include "parser/css/css_keyframe_parser.h"
#include <cctype>
#include <algorithm>

namespace linweb {

CSSParser::CSSParser(std::string source) : tokenizer(std::move(source)) {}

StyleSheet CSSParser::parse(const std::string& source) {
    CSSParser parser(source);
    StyleSheet sheet;
    parser.parse_rules(sheet);
    return sheet;
}

std::vector<Selector> CSSParser::parse_selectors(const std::string& source) {
    CSSParser parser(source);
    return linweb::parse_selectors(parser.tokenizer);
}

std::vector<Rule> CSSParser::parse_rules(StyleSheet& sheet) {
    std::vector<Rule> rules;
    while (true) {
        tokenizer.consume_whitespace();
        if (tokenizer.pos >= tokenizer.input.size()) break;
        
        if (tokenizer.input.substr(tokenizer.pos, 10) == "@keyframes") {
            KeyframesRule kr = parse_keyframes(tokenizer);
            sheet.keyframes[kr.name] = kr;
        } else {
            sheet.rules.push_back(parse_rule());
        }
    }
    return sheet.rules;
}

Rule CSSParser::parse_rule() {
    Rule rule;
    rule.selectors = linweb::parse_selectors(tokenizer);
    tokenizer.consume_whitespace();
    if (tokenizer.pos < tokenizer.input.size() && tokenizer.input[tokenizer.pos] == '{') {
        tokenizer.consume_char(); // {
        rule.declarations = parse_declarations(tokenizer);
        tokenizer.consume_whitespace();
        if (tokenizer.pos < tokenizer.input.size() && tokenizer.input[tokenizer.pos] == '}') tokenizer.consume_char(); // }
    }
    return rule;
}

} // namespace linweb
