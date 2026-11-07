#pragma once
#include <string>
#include <functional>

namespace linweb {

class HTMLTokenizer {
public:
    HTMLTokenizer(std::string source);
    char consume_char();
    std::string consume_while(std::function<bool(char)> test);
    void consume_whitespace();
    bool starts_with(const std::string& s) const;
    
    size_t pos;
    std::string input;
};

} // namespace linweb
