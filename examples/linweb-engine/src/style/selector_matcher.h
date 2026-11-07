#pragma once
#include "style/style_engine.h"

namespace linweb {

class SelectorMatcher {
public:
    static bool matches_simple_selector(
        const std::shared_ptr<Node>& node,
        const SimpleSelector& simple,
        const std::shared_ptr<Node>& hovered_node,
        const std::shared_ptr<Node>& focused_node,
        const std::shared_ptr<Node>& active_node);

    static bool matches_pseudo_class(
        const std::shared_ptr<Node>& node,
        const std::string& pseudo,
        const std::shared_ptr<Node>& hovered_node,
        const std::shared_ptr<Node>& focused_node,
        const std::shared_ptr<Node>& active_node);

    static bool matches_selector(
        const std::shared_ptr<Node>& node,
        const Selector& selector,
        const std::shared_ptr<Node>& hovered_node,
        const std::shared_ptr<Node>& focused_node,
        const std::shared_ptr<Node>& active_node);

    static int calculate_specificity(const Selector& selector);
};

} // namespace linweb
