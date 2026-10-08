#include "parser/css/css_declaration_parser.h"
#include <cctype>

namespace linweb {

std::string parse_property(CSSTokenizer& t) {
    return t.parse_identifier();
}

std::string parse_value(CSSTokenizer& t) {
    return t.consume_while([](char c) { return c != ';' && c != '}'; });
}

Declaration parse_declaration(CSSTokenizer& t) {
    Declaration decl;
    decl.name = parse_property(t);
    t.consume_whitespace();
    if (t.pos < t.input.size() && t.input[t.pos] == ':') {
        t.consume_char(); // :
        t.consume_whitespace();
        decl.value = parse_value(t);
        if (t.pos < t.input.size() && t.input[t.pos] == ';') t.consume_char();
    }
    return decl;
}

std::vector<Declaration> parse_declarations(CSSTokenizer& t) {
    std::vector<Declaration> declarations;
    while (true) {
        t.consume_whitespace();
        if (t.pos >= t.input.size()) break;
        if (t.input[t.pos] == '}') break;
        declarations.push_back(parse_declaration(t));
    }
    return declarations;
}

} // namespace linweb
