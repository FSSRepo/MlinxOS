#pragma once
#include <string>
#include <functional>

namespace linweb {

class CSSTokenizer {
public:
    CSSTokenizer(std::string source);
    void consume_whitespace();
    char consume_char();
    std::string consume_while(std::function<bool(char)> test);
    std::string parse_identifier();
    std::string parse_string();

    size_t pos;
    std::string input;
};

} // namespace linweb
