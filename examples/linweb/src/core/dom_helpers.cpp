#include "core/dom_helpers.h"
#include "parser/html/html_parser.h"

namespace linweb {

size_t count_dom_nodes(const std::shared_ptr<Node>& node) {
    if (!node) return 0;
    size_t count = 1;
    for (const auto& child : node->children) {
        count += count_dom_nodes(child);
    }
    return count;
}

std::shared_ptr<Node> find_node_by_id(const std::shared_ptr<Node>& node, const std::string& id) {
    if (!node) return nullptr;
    if (node->type() == NodeType::Element && node->data.id() == id) {
        return node;
    }
    for (auto& child : node->children) {
        auto found = find_node_by_id(child, id);
        if (found) return found;
    }
    return nullptr;
}

std::vector<std::shared_ptr<ElementNode>> find_nodes_by_tag(const std::shared_ptr<Node>& node, const std::string& tag_name) {
    std::vector<std::shared_ptr<ElementNode>> found;
    if (!node) return found;
    if (node->type() == NodeType::Element) {
        auto element = std::static_pointer_cast<ElementNode>(node);
        if (element->tag_name == tag_name) {
            found.push_back(element);
        }
    }
    for (auto& child : node->children) {
        auto child_found = find_nodes_by_tag(child, tag_name);
        found.insert(found.end(), child_found.begin(), child_found.end());
    }
    return found;
}

std::vector<std::shared_ptr<ElementNode>> find_nodes_by_class(const std::shared_ptr<Node>& node, const std::string& class_name) {
    std::vector<std::shared_ptr<ElementNode>> found;
    if (!node) return found;
    if (node->type() == NodeType::Element) {
        auto element = std::static_pointer_cast<ElementNode>(node);
        auto classes = element->data.classes();
        if (std::find(classes.begin(), classes.end(), class_name) != classes.end()) {
            found.push_back(element);
        }
    }
    for (auto& child : node->children) {
        auto child_found = find_nodes_by_class(child, class_name);
        found.insert(found.end(), child_found.begin(), child_found.end());
    }
    return found;
}

std::string get_text_content(const std::shared_ptr<Node>& node) {
    if (!node) return "";
    if (node->type() == NodeType::Text) {
        return std::static_pointer_cast<TextNode>(node)->data;
    }
    std::string result;
    for (const auto& child : node->children) {
        result += get_text_content(child);
    }
    return result;
}

std::string get_inner_html(const std::shared_ptr<Node>& node) {
    if (!node) return "";
    std::string result;
    for (const auto& child : node->children) {
        if (child->type() == NodeType::Text) {
            result += std::static_pointer_cast<TextNode>(child)->data;
        } else if (child->type() == NodeType::Element) {
            auto el = std::static_pointer_cast<ElementNode>(child);
            result += "<" + el->tag_name;
            for (const auto& attr : el->data.attributes) {
                result += " " + attr.first + "=\"" + attr.second + "\"";
            }
            result += ">";
            result += get_inner_html(child);
            result += "</" + el->tag_name + ">";
        }
    }
    return result;
}

void set_inner_html(const std::shared_ptr<Node>& node, const std::string& html) {
    if (!node) return;
    node->children.clear();
    if (!html.empty()) {
        auto fragment = HTMLParser::parse(html);
        if (fragment) {
            if (fragment->tag_name == "root") {
                for (auto& child : fragment->children) {
                    node->children.push_back(child);
                    child->parent = node;
                }
            } else {
                node->children.push_back(fragment);
                fragment->parent = node;
            }
        }
    }
}

std::vector<std::shared_ptr<Node>> get_element_children(const std::shared_ptr<Node>& node) {
    if (!node) return {};
    std::vector<std::shared_ptr<Node>> result;
    for (const auto& child : node->children) {
        if (child->is_element()) {
            result.push_back(child);
        }
    }
    return result;
}

std::vector<std::shared_ptr<Node>> get_text_children(const std::shared_ptr<Node>& node) {
    if (!node) return {};
    std::vector<std::shared_ptr<Node>> result;
    for (const auto& child : node->children) {
        if (child->is_text()) {
            result.push_back(child);
        }
    }
    return result;
}

} // namespace linweb
