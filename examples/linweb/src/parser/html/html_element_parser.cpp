#include "parser/html/html_element_parser.h"
#include <cctype>

namespace linweb {

std::string parse_tag_name(HTMLTokenizer& t) {
    return t.consume_while([](char c) { 
        return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'; 
    });
}

std::unordered_map<std::string, std::string> parse_attributes(HTMLTokenizer& t) {
    std::unordered_map<std::string, std::string> attrs;
    while (true) {
        t.consume_whitespace();
        if (t.pos >= t.input.size()) break;
        if (t.input[t.pos] == '>') break;
        
        std::string name = parse_tag_name(t);
        t.consume_whitespace();
        if (t.pos >= t.input.size()) break;
        if (t.input[t.pos] == '=') {
            t.consume_char(); // =
            t.consume_whitespace();
            char quote = t.consume_char(); // " or '
            std::string value = t.consume_while([quote](char c) { return c != quote; });
            t.consume_char(); // quote
            attrs[name] = value;
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
