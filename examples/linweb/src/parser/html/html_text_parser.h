#pragma once
#include "parser/html/html_tokenizer.h"
#include "core/dom.h"
#include <memory>
#include <string>

namespace linweb {

bool is_raw_text_element(const std::string& tag_name);
std::shared_ptr<Node> parse_text(HTMLTokenizer& t);
std::shared_ptr<Node> parse_raw_text(HTMLTokenizer& t, const std::string& tag_name);

} // namespace linweb
