#include "parser/css/css_declaration_parser.h"
#include <cctype>
#include <sstream>

namespace linweb {

std::string parse_property(CSSTokenizer& t) {
    return t.parse_identifier();
}

std::string parse_value(CSSTokenizer& t) {
    size_t start = t.pos;
    int depth = 0;
    char quote = 0;
    while (t.pos < t.input.size()) {
        char c = t.input[t.pos];
        if (c == '\\' && t.pos + 1 < t.input.size()) { t.pos += 2; continue; }
        if (quote) { if (c == quote) quote = 0; }
        else if (c == '\'' || c == '"') quote = c;
        else if (c == '(' || c == '[') ++depth;
        else if (c == ')' || c == ']') { if (depth > 0) --depth; }
        else if (c == '}' || (c == ';' && depth == 0)) break;
        ++t.pos;
    }
    return t.input.substr(start, t.pos - start);
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
        size_t before = t.pos;
        auto declaration = parse_declaration(t);
        if (!declaration.name.empty() && !declaration.value.empty()) {
            if (declaration.name == "font") {
                // Common size[/line-height] family shorthand, including the
                // font weight/style prefix used by real page stylesheets.
                std::istringstream words(declaration.value);
                std::string word;
                while (words >> word) {
                    if (word == "bold" || word == "normal" || word == "400" || word == "500" || word == "600" || word == "700")
                        declarations.push_back({"font-weight", word});
                    if ((!word.empty() && (std::isdigit(static_cast<unsigned char>(word[0])) || word[0] == '.') &&
                        word.find_first_of("%./") != std::string::npos) || word.find("px") != std::string::npos || word.find("pt") != std::string::npos || word.find("em") != std::string::npos) {
                        size_t slash = word.find('/');
                        declarations.push_back({"font-size", word.substr(0, slash)});
                        if (slash != std::string::npos) declarations.push_back({"line-height", word.substr(slash + 1)});
                        std::string family;
                        std::getline(words, family);
                        if (!family.empty()) declarations.push_back({"font-family", family});
                        break;
                    }
                }
            } else declarations.push_back(std::move(declaration));
        }
        if (t.pos == before) {
            // Recover from malformed/unsupported syntax instead of spinning.
            while (t.pos < t.input.size() && t.input[t.pos] != ';' && t.input[t.pos] != '}') ++t.pos;
            if (t.pos < t.input.size() && t.input[t.pos] == ';') ++t.pos;
        }
    }
    return declarations;
}

} // namespace linweb
