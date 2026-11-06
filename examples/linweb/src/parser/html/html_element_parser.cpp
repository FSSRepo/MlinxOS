#include "parser/html/html_element_parser.h"
#include <cctype>
#include <algorithm>
#include "utils/text_encoding.h"

namespace linweb {

std::string parse_tag_name(HTMLTokenizer& t) {
    std::string name = t.consume_while([](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'; 
    });
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name;
}

std::unordered_map<std::string, std::string> parse_attributes(HTMLTokenizer& t) {
    std::unordered_map<std::string, std::string> attrs;
    while (true) {
        t.consume_whitespace();
        if (t.pos >= t.input.size()) break;
        if (t.input[t.pos] == '>' || (t.input[t.pos] == '/' && t.pos + 1 < t.input.size() && t.input[t.pos + 1] == '>')) break;
        
        std::string name = t.consume_while([](char c) { return !std::isspace(static_cast<unsigned char>(c)) && c != '=' && c != '>' && c != '/'; });
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name.empty()) { ++t.pos; continue; }
        t.consume_whitespace();
        if (t.pos >= t.input.size()) break;
        if (t.input[t.pos] == '=') {
            t.consume_char(); // =
            t.consume_whitespace();
            if (t.pos >= t.input.size()) { attrs.emplace(name, ""); break; }
            char quote = t.input[t.pos];
            std::string value;
            if (quote == '\'' || quote == '"') {
                ++t.pos;
                value = t.consume_while([quote](char c) { return c != quote; });
                if (t.pos < t.input.size()) ++t.pos;
            } else value = t.consume_while([](char c) { return !std::isspace(static_cast<unsigned char>(c)) && c != '>'; });
            attrs.emplace(name, decode_html_entities(value));
        } else {
            attrs[name] = "";
        }
    }
    return attrs;
}

OpeningTag parse_opening_tag(HTMLTokenizer& t) {
    OpeningTag tag;
    t.consume_char(); // <
    tag.tag_name = parse_tag_name(t);
    tag.attrs = parse_attributes(t);
    t.consume_whitespace();
    tag.self_closing = parse_self_closing_tag(t);
    t.consume_whitespace();
    if (t.pos < t.input.size() && t.input[t.pos] == '>') {
        t.consume_char(); // >
    }
    return tag;
}

std::string parse_closing_tag(HTMLTokenizer& t) {
    t.pos += 2; // </
    t.consume_whitespace();
    std::string tag_name = parse_tag_name(t);
    t.consume_whitespace();
    if (t.pos < t.input.size() && t.input[t.pos] == '>') {
        t.consume_char(); // >
    }
    return tag_name;
}

bool parse_self_closing_tag(HTMLTokenizer& t) {
    if (t.pos < t.input.size() && t.input[t.pos] == '/') {
        t.consume_char();
        return true;
    }
    return false;
}

} // namespace linweb
