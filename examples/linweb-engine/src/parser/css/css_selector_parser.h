#pragma once
#include "parser/css/css_tokenizer.h"
#include <string>
#include <vector>

namespace linweb {

enum class Combinator {
    None,
    Descendant,
    Child,
    AdjacentSibling,
    GeneralSibling
};

struct SimpleSelector {
    struct Attribute { std::string name, op, value; bool insensitive = false; };
    std::string tag_name;
    std::string id;
    std::vector<std::string> classes;
    std::vector<std::string> pseudo_classes;
    std::string pseudo_element;
    std::vector<Attribute> attributes;
};

struct Selector {
    std::vector<SimpleSelector> parts;
    std::vector<Combinator> combinators;
};

Combinator parse_combinator(CSSTokenizer& t, bool has_prev_part);
std::string parse_pseudo_class(CSSTokenizer& t);
SimpleSelector parse_simple_selector(CSSTokenizer& t);
Selector parse_selector(CSSTokenizer& t);
std::vector<Selector> parse_selectors(CSSTokenizer& t);

} // namespace linweb
