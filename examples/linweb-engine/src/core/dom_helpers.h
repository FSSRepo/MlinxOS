#pragma once
#include <vector>
#include <string>
#include <memory>
#include "core/dom.h"

namespace linweb {

/** Counts all nodes in the subtree recursively */
size_t count_dom_nodes(const std::shared_ptr<Node>& node);

/** Finds the first element node with the specified id */
std::shared_ptr<Node> find_node_by_id(const std::shared_ptr<Node>& node, const std::string& id);

/** Finds all element nodes with the specified tag */
std::vector<std::shared_ptr<ElementNode>> find_nodes_by_tag(const std::shared_ptr<Node>& node, const std::string& tag_name);

/** Finds all element nodes with the specified class */
std::vector<std::shared_ptr<ElementNode>> find_nodes_by_class(const std::shared_ptr<Node>& node, const std::string& class_name);

/** Extracts all the text content concatenated recursively */
std::string get_text_content(const std::shared_ptr<Node>& node);

/** Serializes the node's children to an HTML string */
std::string get_inner_html(const std::shared_ptr<Node>& node);

/** Parses HTML and replaces the node's children with the result */
void set_inner_html(const std::shared_ptr<Node>& node, const std::string& html);

/** Filters and returns only the children that are elements */
std::vector<std::shared_ptr<Node>> get_element_children(const std::shared_ptr<Node>& node);

/** Filters and returns only the children that are text nodes */
std::vector<std::shared_ptr<Node>> get_text_children(const std::shared_ptr<Node>& node);

} // namespace linweb
