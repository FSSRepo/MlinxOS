#include "style/property_resolver.h"
#include "style/selector_matcher.h"
#include <sstream>

namespace linweb {

void PropertyResolver::resolve_default_values(PropertyMap& values, const std::string& tag) {
    if (tag == "div" || tag == "h1" || tag == "h2" || tag == "h3" || 
        tag == "p" || tag == "body" || tag == "html" || tag == "section" || 
        tag == "nav" || tag == "header" || tag == "footer" ||
        tag == "dl" || tag == "dt" || tag == "dd") {
        values["display"] = "block";
        if (tag == "dd" && values.find("margin-left") == values.end()) {
            values["margin-left"] = "40px";
        }
        if (tag == "h1") {
            values["font-weight"] = "bold";
            values["font-size"] = "32px";
        } else if (tag == "h2") {
            values["font-weight"] = "bold";
            values["font-size"] = "24px";
        } else if (tag == "h3") {
            values["font-weight"] = "bold";
            values["font-size"] = "18.72px";
        } else if (tag == "p") {
            values["font-size"] = "16px";
        }
    } else if (tag == "span" || tag == "a" || tag == "em" || tag == "strong" || tag == "code" || tag == "br") {
        values["display"] = "inline";
        if (tag == "strong") {
            values["font-weight"] = "bold";
        }
        if (tag == "code") {
            values["font-family"] = "monospace";
        }
    } else if (tag == "button") {
        values["display"] = "inline-block";
        values["background"] = "red";
        values["border"] = "1px solid #767676";
        values["border-radius"] = "2px";
        values["padding"] = "3px 10px";
        values["color"] = "#000";
        values["font-size"] = "13.3px";
        values["margin"] = "2px";
    } else if (tag == "img") {
        values["display"] = "inline";
    } else if (tag == "style" || tag == "script" || tag == "head" || tag == "link") {
        values["display"] = "none";
    }
    
    if (values.find("font-size") == values.end() && (tag == "body" || tag == "html")) {
        values["font-size"] = "16px";
    }
    if (tag == "html" && values.find("overflow") == values.end()) {
        values["overflow"] = "auto";
    }
    if (tag == "body" && values.find("margin") == values.end()) {
        values["margin"] = "8px";
    }
}

void PropertyResolver::apply_cascade(PropertyMap& values, const StyleSheet& stylesheet,
    const std::shared_ptr<Node>& node,
    const std::shared_ptr<Node>& hovered_node,
    const std::shared_ptr<Node>& focused_node,
    const std::shared_ptr<Node>& active_node) {
    
    for (const auto& rule : stylesheet.rules) {
        for (const auto& selector : rule.selectors) {
            if (SelectorMatcher::matches_selector(node, selector, hovered_node, focused_node, active_node)) {
                for (const auto& decl : rule.declarations) {
                    values[decl.name] = decl.value;
                }
            }
        }
    }
}

PropertyMap PropertyResolver::resolve_specified_values(
    const std::shared_ptr<Node>& node,
    const StyleSheet& stylesheet,
    const std::shared_ptr<Node>& hovered_node,
    const std::shared_ptr<Node>& focused_node,
    const std::shared_ptr<Node>& active_node) {
    
    PropertyMap values;
    const std::string& tag = node->tag_name;
    
    resolve_default_values(values, tag);
    
    auto normalize_length_attr = [&](const std::string& raw) -> std::string {
        std::string v = trim(raw);
        if (v.empty()) return v;
        bool is_numeric = true;
        bool saw_dot = false;
        for (char c : v) {
            if (c == '.') {
                if (saw_dot) { is_numeric = false; break; }
                saw_dot = true;
                continue;
            }
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                is_numeric = false;
                break;
            }
        }
        if (is_numeric) return v + "px";
        return v;
    };

    if (tag == "img") {
        auto width_it = node->data.attributes.find("width");
        if (width_it != node->data.attributes.end() && !width_it->second.empty()) {
            values["width"] = normalize_length_attr(width_it->second);
        }
        auto height_it = node->data.attributes.find("height");
        if (height_it != node->data.attributes.end() && !height_it->second.empty()) {
            values["height"] = normalize_length_attr(height_it->second);
        }
    }

    apply_cascade(values, stylesheet, node, hovered_node, focused_node, active_node);

    auto attr_it = node->data.attributes.find("style");
    if (attr_it != node->data.attributes.end()) {
        std::stringstream ss(attr_it->second);
        std::string declaration;
        while (std::getline(ss, declaration, ';')) {
            size_t colon = declaration.find(':');
            if (colon != std::string::npos) {
                std::string name = trim(declaration.substr(0, colon));
                std::string value = trim(declaration.substr(colon + 1));
                if (!name.empty() && !value.empty()) {
                    values[name] = value;
                }
            }
        }
    }

    for (const auto& [name, value] : node->style_overrides) {
        values[name] = value;
    }
    
    return values;
}

void PropertyResolver::resolve_inherited_values(PropertyMap& values, const PropertyMap& inherited_values) {
    static const std::vector<std::string> inheritable_props = {
        "color", "font-family", "font-size", "font-weight", "line-height", "text-align", "opacity",
        "text-shadow"
    };

    for (const auto& prop : inheritable_props) {
        if (inherited_values.count(prop) && !values.count(prop)) {
            values[prop] = inherited_values.at(prop);
        }
    }
}

void PropertyResolver::resolve_currentColor(PropertyMap& values, const PropertyMap& inherited_values) {
    if (values.count("color") && values["color"] == "currentColor") {
        if (inherited_values.count("color")) {
            values["color"] = inherited_values.at("color");
        } else {
            values["color"] = "black";
        }
    }

    std::string current_color = values.count("color") ? values["color"] : "black";
    for (auto& [prop, val] : values) {
        if (val == "currentColor" && prop != "color") {
            val = current_color;
        }
    }
}

} // namespace linweb
