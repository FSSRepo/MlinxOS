#pragma once
#include <memory>
#include <string>
#include <vector>
#include "../core/dom.h"
#include "../parser/css/css_parser.h"
#include "../style/style_engine.h"

namespace linweb {

std::shared_ptr<StyledNode> find_by_class(std::shared_ptr<StyledNode> node, const std::string& class_name);
std::shared_ptr<StyledNode> find_by_id(std::shared_ptr<StyledNode> node, const std::string& id);
void find_tags(std::shared_ptr<Node> node, const std::string& tag_name, std::vector<std::shared_ptr<ElementNode>>& found);
void find_nodes_by_selector(std::shared_ptr<Node> node, const std::vector<Selector>& selectors, std::vector<std::shared_ptr<Node>>& found);
std::shared_ptr<Node> find_node_by_id_recursive(std::shared_ptr<Node> node, const std::string& id);
std::shared_ptr<Node> find_shared_ptr_by_addr(Node* ptr);

} // namespace linweb
