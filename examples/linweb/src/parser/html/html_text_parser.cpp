#include "parser/html/html_text_parser.h"
#include <cctype>
#include <algorithm>

namespace linweb {

bool is_raw_text_element(const std::string& tag_name) {
    return tag_name == "script" || tag_name == "style" || tag_name == "svg";
}

std::shared_ptr<Node> parse_text(HTMLTokenizer& t) {
    std::string text = t.consume_while([](char c) { return c != '<'; });
    
    if (text.empty()) return nullptr;

    std::string result;
    bool last_was_space = false;
    bool has_non_whitespace = false;

    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!last_was_space) {
                result += ' ';
                last_was_space = true;
            }
        } else {
            result += c;
            last_was_space = false;
            has_non_whitespace = true;
        }
    }

    if (result.length() == 1 && (result == " " || result == "\n")) return nullptr;
    
    return std::make_shared<TextNode>(result);
}

std::shared_ptr<Node> parse_raw_text(HTMLTokenizer& t, const std::string& tag_name) {
    std::string closing_tag = "</" + tag_name + ">";
    std::string text;
    
    while (t.pos < t.input.size() && !t.starts_with(closing_tag)) {
        text += t.consume_char();
    }
    
    if (t.starts_with(closing_tag)) {
        t.pos += closing_tag.size();
    }
    
    if (text.empty()) return nullptr;
    return std::make_shared<TextNode>(text);
}

} // namespace linweb
