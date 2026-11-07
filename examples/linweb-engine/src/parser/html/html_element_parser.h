#pragma once
#include "parser/html/html_tokenizer.h"
#include <string>
#include <unordered_map>

namespace linweb {

struct OpeningTag {
    std::string tag_name;
    std::unordered_map<std::string, std::string> attrs;
    bool self_closing;
};

std::string parse_tag_name(HTMLTokenizer& t);
std::unordered_map<std::string, std::string> parse_attributes(HTMLTokenizer& t);
OpeningTag parse_opening_tag(HTMLTokenizer& t);
std::string parse_closing_tag(HTMLTokenizer& t);
bool parse_self_closing_tag(HTMLTokenizer& t);

} // namespace linweb
