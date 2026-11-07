#include "parser/css/css_parser.h"
#include "parser/css/css_selector_parser.h"
#include "parser/css/css_declaration_parser.h"
#include "parser/css/css_keyframe_parser.h"
#include <cctype>
#include <algorithm>
#include <regex>
#include <sstream>

namespace linweb {

CSSParser::CSSParser(std::string source) : tokenizer(std::move(source)) {}

StyleSheet CSSParser::parse(const std::string& source, float viewport_width, float viewport_height) {
    CSSParser parser(source);
    parser.viewport_width_ = viewport_width;
    parser.viewport_height_ = viewport_height;
    StyleSheet sheet;
    parser.parse_rules(sheet);
    for (const auto& rule : sheet.rules) {
        for (const auto& selector : rule.selectors) {
            if (selector.parts.empty()) continue;
            const std::string& pseudo = selector.parts.back().pseudo_element;
            if (pseudo == "first-line") sheet.has_first_line_rules = true;
            if (pseudo == "-webkit-scrollbar") sheet.has_scrollbar_rules = true;
        }
    }
    return sheet;
}

std::vector<Selector> CSSParser::parse_selectors(const std::string& source) {
    CSSParser parser(source);
    return linweb::parse_selectors(parser.tokenizer);
}

// The value of a font-face descriptor without its surrounding quotes and spaces
static std::string unquoted(std::string value) {
    size_t start = value.find_first_not_of(" \t\r\n");
    size_t end = value.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    value = value.substr(start, end - start + 1);
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
        value = value.substr(1, value.size() - 2);
    return value;
}

// The body of an @font-face rule: its family, the file of its src, its weight and its style
static FontFaceRule parse_font_face(const std::string& body) {
    CSSTokenizer tokenizer(body);
    FontFaceRule rule;
    for (const auto& declaration : parse_declarations(tokenizer)) {
        if (declaration.name == "font-family") {
            rule.family = unquoted(declaration.value);
        } else if (declaration.name == "src") {
            size_t url = declaration.value.find("url(");
            if (url == std::string::npos) continue;
            size_t close = declaration.value.find(')', url);
            if (close == std::string::npos) continue;
            rule.source = unquoted(declaration.value.substr(url + 4, close - (url + 4)));
        } else if (declaration.name == "font-weight") {
            rule.weight = unquoted(declaration.value);
        } else if (declaration.name == "font-style") {
            rule.style = unquoted(declaration.value);
        }
    }
    return rule;
}

std::vector<Rule> CSSParser::parse_rules(StyleSheet& sheet) {
    std::vector<Rule> rules;
    while (true) {
        tokenizer.consume_whitespace();
        if (tokenizer.pos >= tokenizer.input.size()) break;
        
        size_t before = tokenizer.pos;
        if (tokenizer.input.substr(tokenizer.pos, 10) == "@keyframes") {
            KeyframesRule kr = parse_keyframes(tokenizer);
            sheet.keyframes[kr.name] = kr;
        } else if (tokenizer.input[tokenizer.pos] == '@') {
            ++tokenizer.pos;
            std::string name = tokenizer.parse_identifier();
            size_t start = tokenizer.pos;
            char quote = 0;
            int parens = 0;
            while (tokenizer.pos < tokenizer.input.size()) {
                char c = tokenizer.input[tokenizer.pos];
                if (c == '\\' && tokenizer.pos + 1 < tokenizer.input.size()) { tokenizer.pos += 2; continue; }
                if (quote) { if (c == quote) quote = 0; }
                else if (c == '\'' || c == '"') quote = c;
                else if (c == '(') ++parens;
                else if (c == ')') --parens;
                else if (!parens && (c == '{' || c == ';')) break;
                ++tokenizer.pos;
            }
            std::string condition = tokenizer.input.substr(start, tokenizer.pos - start);
            if (tokenizer.pos == tokenizer.input.size()) break;
            if (tokenizer.input[tokenizer.pos++] == ';') continue;
            start = tokenizer.pos;
            int depth = 1;
            quote = 0;
            while (tokenizer.pos < tokenizer.input.size() && depth) {
                char c = tokenizer.consume_char();
                if (c == '\\' && tokenizer.pos < tokenizer.input.size()) { ++tokenizer.pos; continue; }
                if (quote) { if (c == quote) quote = 0; }
                else if (c == '\'' || c == '"') quote = c;
                else if (c == '{') ++depth;
                else if (c == '}') --depth;
                else if (c == '/' && tokenizer.pos < tokenizer.input.size() && tokenizer.input[tokenizer.pos] == '*') {
                    size_t end = tokenizer.input.find("*/", tokenizer.pos + 1);
                    tokenizer.pos = end == std::string::npos ? tokenizer.input.size() : end + 2;
                }
            }
            if (name == "font-face") {
                size_t body_end = tokenizer.pos - (depth == 0 ? 1 : 0);
                sheet.font_faces.push_back(parse_font_face(tokenizer.input.substr(start, body_end - start)));
            }
            if (name == "media") {
                bool matches = false;
                std::istringstream alternatives(condition);
                std::string query;
                while (std::getline(alternatives, query, ',')) {
                    std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    bool accepted = query.find("print") == std::string::npos && query.find("not ") == std::string::npos;
                    static const std::regex feature(R"(\(\s*([a-z-]+)\s*:\s*([^)]*)\))");
                    for (std::sregex_iterator it(query.begin(), query.end(), feature), end; it != end; ++it) {
                        std::string key = (*it)[1], value = (*it)[2];
                        if (key == "min-width" || key == "max-width" || key == "min-height" || key == "max-height") {
                            try {
                                size_t unit_pos;
                                float limit = std::stof(value, &unit_pos);
                                std::string unit = value.substr(unit_pos);
                                unit.erase(std::remove_if(unit.begin(), unit.end(), [](unsigned char c) { return std::isspace(c); }), unit.end());
                                if (unit == "em" || unit == "rem") limit *= 16;
                                else if (unit != "px" && !unit.empty()) { accepted = false; continue; }
                                float actual = key.find("width") != std::string::npos ? viewport_width_ : viewport_height_;
                                accepted = accepted && (key.compare(0, 3, "min") == 0 ? actual >= limit : actual <= limit);
                            } catch (...) { accepted = false; }
                        } else if (key == "prefers-color-scheme") accepted = accepted && value.find("light") != std::string::npos;
                        else if (key == "forced-colors") accepted = accepted && value.find("none") != std::string::npos;
                        else accepted = false;
                    }
                    std::string remaining = std::regex_replace(query, feature, " ");
                    std::istringstream words(remaining);
                    std::string word;
                    while (words >> word) if (word != "screen" && word != "all" && word != "and" && word != "only") accepted = false;
                    matches = matches || accepted;
                }
                if (matches) {
                    size_t end = tokenizer.pos - (depth == 0 ? 1 : 0);
                    auto nested = CSSParser::parse(tokenizer.input.substr(start, end - start), viewport_width_, viewport_height_);
                    sheet.rules.insert(sheet.rules.end(), nested.rules.begin(), nested.rules.end());
                    sheet.keyframes.insert(nested.keyframes.begin(), nested.keyframes.end());
                    sheet.font_faces.insert(sheet.font_faces.end(), nested.font_faces.begin(), nested.font_faces.end());
                }
            }
            // Unknown at-rules and unsupported font faces are skipped as whole
            // blocks, keeping following rules intact.
        } else {
            auto rule = parse_rule();
            if (!rule.selectors.empty() && !rule.declarations.empty()) sheet.rules.push_back(std::move(rule));
        }
        if (tokenizer.pos == before) ++tokenizer.pos;
    }
    return sheet.rules;
}

Rule CSSParser::parse_rule() {
    Rule rule;
    rule.selectors = linweb::parse_selectors(tokenizer);
    tokenizer.consume_whitespace();
    // Discard an unsupported selector as a whole, never apply its prefix.
    if (tokenizer.pos < tokenizer.input.size() && tokenizer.input[tokenizer.pos] != '{') {
        rule.selectors.clear();
        while (tokenizer.pos < tokenizer.input.size() && tokenizer.input[tokenizer.pos] != '{' && tokenizer.input[tokenizer.pos] != '}') ++tokenizer.pos;
    }
    if (tokenizer.pos < tokenizer.input.size() && tokenizer.input[tokenizer.pos] == '{') {
        tokenizer.consume_char(); // {
        rule.declarations = parse_declarations(tokenizer);
        tokenizer.consume_whitespace();
        if (tokenizer.pos < tokenizer.input.size() && tokenizer.input[tokenizer.pos] == '}') tokenizer.consume_char(); // }
    }
    return rule;
}

} // namespace linweb
