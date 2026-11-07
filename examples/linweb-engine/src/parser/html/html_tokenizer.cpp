#include "parser/html/html_tokenizer.h"
#include <cctype>

namespace linweb {

HTMLTokenizer::HTMLTokenizer(std::string source) : pos(0), input(std::move(source)) {}

char HTMLTokenizer::consume_char() {
    return input[pos++];
}

std::string HTMLTokenizer::consume_while(std::function<bool(char)> test) {
    std::string result;
    while (pos < input.size() && test(input[pos])) {
        result += consume_char();
    }
    return result;
}

void HTMLTokenizer::consume_whitespace() {
    consume_while([](char c) { return std::isspace(static_cast<unsigned char>(c)); });
}

bool HTMLTokenizer::starts_with(const std::string& s) const {
    if (pos + s.size() > input.size()) return false;
    return input.compare(pos, s.size(), s) == 0;
}

} // namespace linweb
