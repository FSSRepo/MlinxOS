#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <algorithm>

namespace linweb {

enum class NodeType {
    Text,
    Element
};

struct ElementData {
    std::unordered_map<std::string, std::string> attributes;

    std::string id() const {
        auto it = attributes.find("id");
        return it != attributes.end() ? it->second : "";
    }

    std::string src() const {
        auto it = attributes.find("src");
        return it != attributes.end() ? it->second : "";
    }

    std::vector<std::string> classes() const {
        auto it = attributes.find("class");
        if (it == attributes.end()) return {};
        
        std::vector<std::string> result;
        std::string s = it->second;
        size_t start = 0;
        size_t end = s.find(' ');
        while (end != std::string::npos) {
            std::string cls = s.substr(start, end - start);
            if (!cls.empty()) result.push_back(cls);
            start = end + 1;
            end = s.find(' ', start);
        }
        std::string cls = s.substr(start);
        if (!cls.empty()) result.push_back(cls);
        return result;
    }
};

struct Node {
    virtual ~Node() = default;
    
    std::string tag_name;
    std::vector<std::shared_ptr<Node>> children;
    std::weak_ptr<Node> parent;
    ElementData data;
    std::unordered_map<std::string, std::string> style_overrides;
    bool debug_enabled = false;
    
    explicit Node(std::string tag = "") : tag_name(std::move(tag)) {}
    
    virtual NodeType type() const = 0;

    bool is_text() const { return type() == NodeType::Text; }
    bool is_element() const { return type() == NodeType::Element; }

    std::vector<std::shared_ptr<Node>> get_element_children() const {
        std::vector<std::shared_ptr<Node>> result;
        for (const auto& child : children) {
            if (child->is_element()) {
                result.push_back(child);
            }
        }
        return result;
    }

    std::vector<std::shared_ptr<Node>> get_text_children() const {
        std::vector<std::shared_ptr<Node>> result;
        for (const auto& child : children) {
            if (child->is_text()) {
                result.push_back(child);
            }
        }
        return result;
    }

protected:
    void print_indent(int indent) const {
        for (int i = 0; i < indent; ++i) std::cout << "  ";
    }
};

struct TextNode : public Node {
    std::string data;
    
    explicit TextNode(std::string d, std::string tag = "#text") 
        : Node(std::move(tag)), data(std::move(d)) {}
    
    NodeType type() const override { return NodeType::Text; }
};

struct ElementNode : public Node {
    explicit ElementNode(std::string tag, std::unordered_map<std::string, std::string> attrs) 
        : Node(std::move(tag)) {
        data.attributes = std::move(attrs);
    }
    NodeType type() const override { return NodeType::Element; }
};

struct PseudoElementNode : public Node {
    std::string pseudo_type;
    std::string pseudo_content;
    std::weak_ptr<Node> owner;
    
    explicit PseudoElementNode(std::string type, std::string content, std::shared_ptr<Node> owner_node)
        : Node("::" + type), pseudo_type(std::move(type)), pseudo_content(std::move(content)), owner(owner_node) {}
    
    NodeType type() const override { return NodeType::Element; }
};

} // namespace linweb
