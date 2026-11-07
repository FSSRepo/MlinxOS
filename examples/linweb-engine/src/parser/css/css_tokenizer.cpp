#include "parser/css/css_tokenizer.h"
#include <cctype>

namespace linweb {

CSSTokenizer::CSSTokenizer(std::string source) : pos(0), input(std::move(source)) {}

void CSSTokenizer::consume_whitespace() {
    while (pos < input.size()) {
        if (std::isspace(static_cast<unsigned char>(input[pos]))) {
            pos++;
        } else if (pos + 1 < input.size() && input[pos] == '/' && input[pos + 1] == '*') {
            pos += 2; // skip /*
            while (pos + 1 < input.size() && !(input[pos] == '*' && input[pos + 1] == '/')) {
                pos++;
            }
            if (pos + 1 < input.size()) {
                pos += 2; // skip */
            } else {
                pos = input.size();
            }
        } else {
            break;
        }
    }
}

char CSSTokenizer::consume_char() {
    return input[pos++];
}

std::string CSSTokenizer::consume_while(std::function<bool(char)> test) {
    std::string result;
    while (pos < input.size() && test(input[pos])) {
        result += consume_char();
    }
    return result;
}

std::string CSSTokenizer::parse_identifier() {
    return consume_while([](char c) { 
        return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'; 
    });
}

std::string CSSTokenizer::parse_string() {
    if (pos >= input.size()) return "";
    char quote = input[pos];
    if (quote != '"' && quote != '\'') return "";
    consume_char(); // opening quote
    std::string result = consume_while([quote](char c) { return c != quote; });
    if (pos < input.size() && input[pos] == quote) {
        consume_char(); // closing quote
    }
    return result;
}

} // namespace linweb
