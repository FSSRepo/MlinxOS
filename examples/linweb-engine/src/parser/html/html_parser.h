#pragma once
#include "core/dom.h"
#include "parser/html/html_tokenizer.h"
#include <string>
#include <memory>
#include <vector>

namespace linweb {

class HTMLParser {
public:
    static std::shared_ptr<Node> parse(const std::string& source);

private:
    HTMLParser(std::string source);
    std::vector<std::shared_ptr<Node>> parse_nodes();
    std::shared_ptr<Node> parse_element();
    
    HTMLTokenizer tokenizer;
};

} // namespace linweb
