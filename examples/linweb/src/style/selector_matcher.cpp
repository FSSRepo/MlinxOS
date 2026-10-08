#include "style/selector_matcher.h"
#include "parser/css/css_parser.h"
#include <algorithm>

namespace linweb {

bool SelectorMatcher::matches_pseudo_class(
    const std::shared_ptr<Node>& node,
    const std::string& pseudo,
    const std::shared_ptr<Node>& hovered_node,
    const std::shared_ptr<Node>& focused_node,
    const std::shared_ptr<Node>& active_node) {
    
    if (pseudo == "hover") {
        if (!hovered_node) return false;
        std::shared_ptr<Node> current = hovered_node;
        while (current) {
            if (current == node) return true;
            current = current->parent.lock();
        }
        return false;
    } else if (pseudo == "focus") {
        return focused_node && focused_node == node;
    } else if (pseudo == "active") {
        return active_node && active_node == node;
    } else if (pseudo == "first-child") {
        auto parent = node->parent.lock();
        if (!parent) return false;
        auto el_children = parent->get_element_children();
        return !el_children.empty() && el_children[0] == node;
    } else if (pseudo == "last-child") {
        auto parent = node->parent.lock();
        if (!parent) return false;
        auto el_children = parent->get_element_children();
        return !el_children.empty() && el_children.back() == node;
    } else if (pseudo.find("nth-child(") == 0) {
        auto parent = node->parent.lock();
        if (!parent) return false;
        auto el_children = parent->get_element_children();
        size_t index = 0;
        for (size_t i = 0; i < el_children.size(); ++i) {
            if (el_children[i] == node) { index = i + 1; break; }
        }
        if (index == 0) return false;
        std::string arg = pseudo.substr(10);
        if (!arg.empty() && arg.back() == ')') arg.pop_back();
        if (arg == "odd") return index % 2 != 0;
        if (arg == "even") return index % 2 == 0;
        try { return (int)index == std::stoi(arg); } catch (...) { return false; }
    } else if (pseudo.find("nth-last-child(") == 0) {
        auto parent = node->parent.lock();
        if (!parent) return false;
        auto el_children = parent->get_element_children();
        size_t index = 0;
        for (size_t i = 0; i < el_children.size(); ++i) {
            if (el_children[i] == node) { index = el_children.size() - i; break; }
        }
        if (index == 0) return false;
        std::string arg = pseudo.substr(14);
        if (!arg.empty() && arg.back() == ')') arg.pop_back();
        if (arg == "odd") return index % 2 != 0;
        if (arg == "even") return index % 2 == 0;
        try { return (int)index == std::stoi(arg); } catch (...) { return false; }
    } else if (pseudo.find("not(") == 0) {
        std::string arg = pseudo.substr(4);
        if (!arg.empty() && arg.back() == ')') arg.pop_back();
        auto inner_selectors = linweb::CSSParser::parse_selectors(arg);
        if (!inner_selectors.empty() && !inner_selectors[0].parts.empty()) {
            if (matches_simple_selector(node, inner_selectors[0].parts[0], hovered_node, focused_node, active_node)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool SelectorMatcher::matches_simple_selector(
    const std::shared_ptr<Node>& node,
    const SimpleSelector& simple,
    const std::shared_ptr<Node>& hovered_node,
    const std::shared_ptr<Node>& focused_node,
    const std::shared_ptr<Node>& active_node) {

    if (!simple.pseudo_element.empty()) {
        if (node->tag_name != "::" + simple.pseudo_element) {
            return false;
        }
    }

    std::shared_ptr<Node> target = node;
    auto pseudo_node = std::dynamic_pointer_cast<PseudoElementNode>(node);
    if (pseudo_node) {
        target = pseudo_node->owner.lock();
        if (!target) return false;
    }

    if (!simple.tag_name.empty() && simple.tag_name != "*" && simple.tag_name != target->tag_name) {
        return false;
    }

    if (!simple.id.empty() && simple.id != target->data.id()) {
        return false;
    }

    auto node_classes = target->data.classes();
    for (const auto& cls : simple.classes) {
        if (std::find(node_classes.begin(), node_classes.end(), cls) == node_classes.end()) {
            return false;
        }
    }

    for (const auto& pseudo : simple.pseudo_classes) {
        if (!matches_pseudo_class(target, pseudo, hovered_node, focused_node, active_node)) {
            return false;
        }
    }

    return true;
}

static bool matches_selector_chain(
    const std::shared_ptr<Node>& node,
    const Selector& selector,
    size_t part_index,
    const std::shared_ptr<Node>& hovered_node,
    const std::shared_ptr<Node>& focused_node,
    const std::shared_ptr<Node>& active_node) {

    if (part_index >= selector.parts.size()) return false;
    if (!node) return false;

    if (!SelectorMatcher::matches_simple_selector(node, selector.parts[part_index], hovered_node, focused_node, active_node)) {
        return false;
    }

    if (part_index == 0) return true;

    Combinator comb = selector.combinators[part_index - 1];

    if (comb == Combinator::Child) {
        auto parent = node->parent.lock();
        if (!parent) return false;
        return matches_selector_chain(parent, selector, part_index - 1, hovered_node, focused_node, active_node);
    } else if (comb == Combinator::Descendant) {
        auto current = node->parent.lock();
        while (current) {
            if (matches_selector_chain(current, selector, part_index - 1, hovered_node, focused_node, active_node)) {
                return true;
            }
            current = current->parent.lock();
        }
        return false;
    } else if (comb == Combinator::AdjacentSibling) {
        auto parent = node->parent.lock();
        if (!parent) return false;
        auto el_children = parent->get_element_children();
        std::shared_ptr<Node> prev = nullptr;
        for (size_t i = 0; i < el_children.size(); ++i) {
            if (el_children[i] == node) {
                if (!prev) return false;
                return matches_selector_chain(prev, selector, part_index - 1, hovered_node, focused_node, active_node);
            }
            prev = el_children[i];
        }
        return false;
    } else if (comb == Combinator::GeneralSibling) {
        auto parent = node->parent.lock();
        if (!parent) return false;
        auto el_children = parent->get_element_children();
        for (size_t i = 0; i < el_children.size(); ++i) {
            if (el_children[i] == node) {
                for (int j = (int)i - 1; j >= 0; --j) {
                    if (matches_selector_chain(el_children[j], selector, part_index - 1, hovered_node, focused_node, active_node)) {
                        return true;
                    }
                }
                return false;
            }
        }
        return false;
    }

    return false;
}

bool SelectorMatcher::matches_selector(
    const std::shared_ptr<Node>& node,
    const Selector& selector,
    const std::shared_ptr<Node>& hovered_node,
    const std::shared_ptr<Node>& focused_node,
    const std::shared_ptr<Node>& active_node) {

    if (selector.parts.empty()) return false;
    return matches_selector_chain(node, selector, selector.parts.size() - 1, hovered_node, focused_node, active_node);
}

int SelectorMatcher::calculate_specificity(const Selector& selector) {
    int a = 0, b = 0, c = 0;
    for (const auto& part : selector.parts) {
        if (!part.id.empty()) a++;
        b += (int)part.classes.size();
        b += (int)part.pseudo_classes.size();
        if (!part.tag_name.empty() && part.tag_name != "*") c++;
        if (!part.pseudo_element.empty()) c++;
    }
    return a * 100 + b * 10 + c;
}

} // namespace linweb
