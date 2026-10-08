#include "dom_traversal.h"
#include <algorithm>
#include <functional>

namespace linweb {

// Global state referenced by traversal functions
extern std::shared_ptr<Node> g_dom_root;
extern std::vector<std::shared_ptr<Node>> g_detached_nodes;
extern std::shared_ptr<StyledNode> g_hovered_node;
extern std::shared_ptr<Node> g_focused_node;
extern std::shared_ptr<Node> g_active_node;

std::shared_ptr<StyledNode> find_by_class(std::shared_ptr<StyledNode> node, const std::string& class_name) {
    if (!node) return nullptr;
    if (node->node->type() == NodeType::Element) {
        auto element = std::static_pointer_cast<ElementNode>(node->node);
        auto classes = element->data.classes();
        if (std::find(classes.begin(), classes.end(), class_name) != classes.end()) {
            return node;
        }
    }
    for (auto& child : node->children) {
        auto found = find_by_class(child, class_name);
        if (found) return found;
    }
    return nullptr;
}

std::shared_ptr<StyledNode> find_by_id(std::shared_ptr<StyledNode> node, const std::string& id) {
    if (!node) return nullptr;
    if (node->node->type() == NodeType::Element) {
        auto element = std::static_pointer_cast<ElementNode>(node->node);
        if (element->data.id() == id) {
            return node;
        }
    }
    for (auto& child : node->children) {
        auto found = find_by_id(child, id);
        if (found) return found;
    }
    return nullptr;
}

void find_tags(std::shared_ptr<Node> node, const std::string& tag_name, std::vector<std::shared_ptr<ElementNode>>& found) {
    if (!node) return;
    if (node->type() == NodeType::Element) {
        auto element = std::static_pointer_cast<ElementNode>(node);
        if (element->tag_name == tag_name) {
            found.push_back(element);
        }
    }
    for (auto& child : node->children) {
        find_tags(child, tag_name, found);
    }
}

void find_nodes_by_selector(std::shared_ptr<Node> node, const std::vector<Selector>& selectors, std::vector<std::shared_ptr<Node>>& found) {
    if (!node) return;
    if (node->type() == NodeType::Element) {
        for (const auto& selector : selectors) {
            if (StyleEngine::matches(node, selector, g_hovered_node ? g_hovered_node->node : nullptr, g_focused_node, g_active_node)) {
                found.push_back(node);
                break;
            }
        }
    }
    for (auto& child : node->children) {
        find_nodes_by_selector(child, selectors, found);
    }
}

std::shared_ptr<Node> find_node_by_id_recursive(std::shared_ptr<Node> node, const std::string& id) {
    if (!node) return nullptr;
    if (node->type() == NodeType::Element && node->data.id() == id) {
        return node;
    }
    for (auto& child : node->children) {
        auto found = find_node_by_id_recursive(child, id);
        if (found) return found;
    }
    return nullptr;
}

std::shared_ptr<Node> find_shared_ptr_by_addr(Node* ptr) {
    if (!ptr) return nullptr;
    
    std::function<std::shared_ptr<Node>(std::shared_ptr<Node>)> find_in_tree;
    find_in_tree = [&](std::shared_ptr<Node> n) -> std::shared_ptr<Node> {
        if (n.get() == ptr) return n;
        for (auto& c : n->children) {
            auto found = find_in_tree(c);
            if (found) return found;
        }
        return nullptr;
    };
    
    if (g_dom_root) {
        auto found = find_in_tree(g_dom_root);
        if (found) return found;
    }
    
    auto it = std::find_if(g_detached_nodes.begin(), g_detached_nodes.end(),
        [ptr](const std::shared_ptr<Node>& n) { return n.get() == ptr; });
    if (it != g_detached_nodes.end()) return *it;
    
    return nullptr;
}

} // namespace linweb
