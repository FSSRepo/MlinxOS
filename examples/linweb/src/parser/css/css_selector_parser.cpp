#include "parser/css/css_selector_parser.h"
#include <cctype>

namespace linweb {

static std::string parse_pseudo_argument(const std::string& raw) {
    std::string result;
    size_t start = raw.find('(');
    size_t end = raw.rfind(')');
    if (start != std::string::npos && end != std::string::npos && end > start) {
        result = raw.substr(start + 1, end - start - 1);
    }
    return result;
}

static std::string pseudo_name(const std::string& raw) {
    size_t paren = raw.find('(');
    if (paren == std::string::npos) return raw;
    return raw.substr(0, paren);
}

Combinator parse_combinator(CSSTokenizer& t, bool has_prev_part) {
    if (t.pos >= t.input.size()) {
        if (has_prev_part) return Combinator::Descendant;
        return Combinator::None;
    }
    char c = t.input[t.pos];
    if (c == '>') {
        t.consume_char();
        return Combinator::Child;
    } else if (c == '+') {
        t.consume_char();
        return Combinator::AdjacentSibling;
    } else if (c == '~') {
        t.consume_char();
        return Combinator::GeneralSibling;
    } else if (has_prev_part) {
        return Combinator::Descendant;
    }
    return Combinator::None;
}

std::string parse_pseudo_class(CSSTokenizer& t) {
    t.consume_char(); // :
    std::string name = t.parse_identifier();
    if (t.pos < t.input.size() && t.input[t.pos] == '(') {
        t.consume_char(); // (
        size_t depth = 1;
        std::string arg;
        while (t.pos < t.input.size() && depth > 0) {
            if (t.input[t.pos] == '(') {
                depth++;
                arg += t.consume_char();
            } else if (t.input[t.pos] == ')') {
                depth--;
                if (depth > 0) arg += t.consume_char();
                else t.consume_char(); // skip closing )
            } else {
                arg += t.consume_char();
            }
        }
        name += "(" + arg + ")";
    }
    return name;
}

SimpleSelector parse_simple_selector(CSSTokenizer& t) {
    SimpleSelector simple;
    while (t.pos < t.input.size()) {
        char c = t.input[t.pos];
        if (std::isalnum(static_cast<unsigned char>(c))) {
            simple.tag_name = t.parse_identifier();
        } else if (c == '#') {
            t.consume_char();
            simple.id = t.parse_identifier();
        } else if (c == '.') {
            t.consume_char();
            simple.classes.push_back(t.parse_identifier());
        } else if (c == ':') {
            if (t.pos + 1 < t.input.size() && t.input[t.pos + 1] == ':') {
                t.consume_char();
                t.consume_char();
                simple.pseudo_element = t.parse_identifier();
            } else {
                simple.pseudo_classes.push_back(parse_pseudo_class(t));
            }
        } else if (c == '*') {
            t.consume_char();
            simple.tag_name = "*";
        } else {
            break;
        }
    }
    return simple;
}

Selector parse_selector(CSSTokenizer& t) {
    Selector selector;
    while (true) {
        t.consume_whitespace();
        if (t.pos >= t.input.size()) break;
        char c = t.input[t.pos];
        if (c == ',' || c == '{') break;
        
        Combinator comb = parse_combinator(t, !selector.parts.empty());
        if (comb != Combinator::None) {
            selector.combinators.push_back(comb);
        }
        
        SimpleSelector simple = parse_simple_selector(t);
        if (simple.tag_name.empty() && simple.id.empty() && simple.classes.empty() && simple.pseudo_classes.empty() && simple.pseudo_element.empty()) {
            break;
        }
        selector.parts.push_back(std::move(simple));
        
        t.consume_whitespace();
        if (t.pos >= t.input.size()) break;
        c = t.input[t.pos];
        if (c == ',' || c == '{') break;
    }
    return selector;
}

std::vector<Selector> parse_selectors(CSSTokenizer& t) {
    std::vector<Selector> selectors;
    while (true) {
        selectors.push_back(parse_selector(t));
        t.consume_whitespace();
        if (t.pos >= t.input.size() || t.input[t.pos] != ',') break;
        t.consume_char(); // ,
        t.consume_whitespace();
    }
    return selectors;
}

} // namespace linweb
