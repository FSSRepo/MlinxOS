#pragma once
#include "parser/css/css_tokenizer.h"
#include <string>
#include <vector>

namespace linweb {

struct Declaration {
    std::string name;
    std::string value;
};

std::string parse_property(CSSTokenizer& t);
std::string parse_value(CSSTokenizer& t);
Declaration parse_declaration(CSSTokenizer& t);
std::vector<Declaration> parse_declarations(CSSTokenizer& t);

} // namespace linweb
