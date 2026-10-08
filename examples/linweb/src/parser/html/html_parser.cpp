#include "parser/html/html_parser.h"
#include "parser/html/html_element_parser.h"
#include "parser/html/html_text_parser.h"
#include <algorithm>
#include <cctype>
#include <functional>

namespace linweb {

HTMLParser::HTMLParser(std::string source) : tokenizer(std::move(source)) {}

std::shared_ptr<Node> HTMLParser::parse(const std::string& source) {
    HTMLParser parser(source);
    auto nodes = parser.parse_nodes();
    
    if (nodes.size() == 1) {
        return nodes[0];
    } else {
        auto root = std::make_shared<ElementNode>("root", std::unordered_map<std::string, std::string>());
        root->children = std::move(nodes);
        for (auto& child : root->children) {
            child->parent = root;
        }
        return root;
    }
}

std::vector<std::shared_ptr<Node>> HTMLParser::parse_nodes() {
    std::vector<std::shared_ptr<Node>> nodes;
    while (true) {
        if (tokenizer.pos >= tokenizer.input.size()) {
            break;
        }
        if (tokenizer.starts_with("</")) {
            size_t saved_pos = tokenizer.pos;

            tokenizer.pos += 2; // </
            tokenizer.consume_whitespace();
            std::string close_tag = parse_tag_name(tokenizer);
            tokenizer.consume_whitespace();
            if (tokenizer.pos < tokenizer.input.size() && tokenizer.input[tokenizer.pos] == '>') {
                tokenizer.consume_char(); // >
            }

            static const std::vector<std::string> void_elements = {
                "area", "base", "br", "col", "embed", "hr", "img", "input",
                "link", "meta", "param", "source", "track", "wbr"
            };
            bool is_void = std::find(void_elements.begin(), void_elements.end(), close_tag) != void_elements.end();
            if (is_void) {
                continue;
            }

            tokenizer.pos = saved_pos;
            break;
        }
        
        if (tokenizer.starts_with("<!")) {
            tokenizer.consume_while([](char c) { return c != '>'; });
            if (tokenizer.pos < tokenizer.input.size()) tokenizer.consume_char(); // >
            continue;
        }

        auto node = parse_element();
        if (node) {
            nodes.push_back(node);
        }
    }
    return nodes;
}

std::shared_ptr<Node> HTMLParser::parse_element() {
    if (tokenizer.input[tokenizer.pos] != '<') {
        return parse_text(tokenizer);
    }

    auto opening = parse_opening_tag(tokenizer);
    std::string tag_name = opening.tag_name;
    auto attrs = std::move(opening.attrs);
    bool self_closing = opening.self_closing;
    
    std::vector<std::shared_ptr<Node>> children;
    
    static const std::vector<std::string> void_elements = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr"
    };
    
    bool is_void = std::find(void_elements.begin(), void_elements.end(), tag_name) != void_elements.end();

    if (!self_closing && !is_void) {
        if (is_raw_text_element(tag_name)) {
            auto raw = parse_raw_text(tokenizer, tag_name);
            if (raw) children.push_back(raw);
        } else {
            children = parse_nodes();

            if (tokenizer.starts_with("</")) {
                parse_closing_tag(tokenizer);
            }
        }
    }

    auto node = std::make_shared<ElementNode>(tag_name, attrs);
    node->children = std::move(children);
    for (auto& child : node->children) {
        child->parent = node;
    }
    return node;
}

} // namespace linweb
