#include "style/property_resolver.h"
#include "style/selector_matcher.h"
#include "forms/form_controls.h"
#include <sstream>

namespace linweb {

void PropertyResolver::resolve_default_values(PropertyMap& values, const std::string& tag) {
    if (tag == "div" || tag == "h1" || tag == "h2" || tag == "h3" || 
        tag == "p" || tag == "body" || tag == "html" || tag == "section" || 
        tag == "nav" || tag == "header" || tag == "footer" ||
        tag == "dl" || tag == "dt" || tag == "dd" || tag == "center" || tag == "form" ||
        tag == "main" || tag == "article" || tag == "ul" || tag == "ol" || tag == "li") {
        values["display"] = "block";
        if (tag == "dd" && values.find("margin-left") == values.end()) {
            values["margin-left"] = "40px";
        }
        if ((tag == "ul" || tag == "ol") && values.find("padding-left") == values.end()) {
            values["padding-left"] = "40px";   // native indent for the markers of the items
            values["margin-top"] = "16px";
            values["margin-bottom"] = "16px";
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
    } else if (tag == "span" || tag == "a" || tag == "em" || tag == "strong" || tag == "code" || tag == "br" ||
               tag == "b" || tag == "i" || tag == "u" || tag == "s" || tag == "small" || tag == "label" || tag == "abbr" ||
               tag == "cite" || tag == "kbd" || tag == "var" || tag == "mark" || tag == "sub" || tag == "sup") {
        values["display"] = "inline";
        if (tag == "strong" || tag == "b") {
            values["font-weight"] = "bold";
        }
        if (tag == "em" || tag == "i" || tag == "cite" || tag == "var") {
            values["font-style"] = "italic";
        }
        if (tag == "small") values["font-size"] = "13px";
        if (tag == "kbd") { values["font-family"] = "monospace"; values["font-size"] = "13px"; }
        if (tag == "code") {
            values["font-family"] = "monospace";
            values["font-size"] = "13px";   // the browser's default size for the generic monospace family
        }
    } else if (tag == "button") {
        values["display"] = "inline-block";
        values["background"] = "#efefef";
        values["border"] = "1px solid #767676";
        values["border-radius"] = "2px";
        values["padding"] = "2px 7px";   // with the 1px border: the 2px border and 1px 6px padding of a browser button
        values["color"] = "#000";
        values["font-size"] = "13.3px";
        values["margin"] = "0";
    } else if (tag == "img" || tag == "video") {
        values["display"] = "inline";
    } else if (tag == "table") {
        values["display"] = "table";
    } else if (tag == "style" || tag == "script" || tag == "head" || tag == "link" || tag == "meta" || tag == "title" || tag == "base" || tag == "template" || tag == "option") {
        values["display"] = "none";   // an option is drawn by its select, not as a box of its own
    }
    if (tag == "center" || tag == "th") values["text-align"] = "center";
    if (tag == "td") values["text-align"] = "left";
    if (tag == "a") values["color"] = "#0000ee";
    
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

namespace {
// Logical (flow-relative) properties under their physical names, for a horizontal page read left to right. A
// two-value shorthand such as padding-inline: 4px 8px gives its start side and its end side.
std::vector<std::pair<std::string, std::string>> logical_to_physical(const std::string& name, const std::string& value) {
    static const std::map<std::string, std::string> sides = {
        {"margin-inline-start", "margin-left"}, {"margin-inline-end", "margin-right"},
        {"padding-inline-start", "padding-left"}, {"padding-inline-end", "padding-right"},
        {"inset-inline-start", "left"}, {"inset-inline-end", "right"},
        {"margin-block-start", "margin-top"}, {"margin-block-end", "margin-bottom"},
        {"padding-block-start", "padding-top"}, {"padding-block-end", "padding-bottom"}};
    auto side = sides.find(name);
    if (side != sides.end()) return {{side->second, value}};

    static const std::map<std::string, std::pair<std::string, std::string>> pairs = {
        {"margin-inline", {"margin-left", "margin-right"}}, {"padding-inline", {"padding-left", "padding-right"}},
        {"margin-block", {"margin-top", "margin-bottom"}}, {"padding-block", {"padding-top", "padding-bottom"}}};
    std::istringstream in(value);
    std::vector<std::string> parts;
    for (std::string part; in >> part;) parts.push_back(part);

    // inset: top, right, bottom, left, with the usual one-to-four value expansion
    if (name == "inset") {
        if (parts.empty()) return {};
        const std::string& top = parts[0];
        const std::string& right = parts.size() > 1 ? parts[1] : top;
        const std::string& bottom = parts.size() > 2 ? parts[2] : top;
        const std::string& left = parts.size() > 3 ? parts[3] : right;
        return {{"top", top}, {"right", right}, {"bottom", bottom}, {"left", left}};
    }

    auto pair = pairs.find(name);
    if (pair == pairs.end()) return {{name, value}};

    if (parts.empty()) return {};
    if (parts.size() == 1) return {{pair->second.first, parts[0]}, {pair->second.second, parts[0]}};
    return {{pair->second.first, parts[0]}, {pair->second.second, parts[1]}};
}
} // namespace

void PropertyResolver::apply_cascade(PropertyMap& values, const StyleSheet& stylesheet,
    const std::shared_ptr<Node>& node,
    const std::shared_ptr<Node>& hovered_node,
    const std::shared_ptr<Node>& focused_node,
    const std::shared_ptr<Node>& active_node) {
    
    std::map<std::string, int> priorities;
    auto set = [&](const std::string& name, const std::string& value, int priority) {
        auto previous = priorities.find(name);
        if (previous == priorities.end() || priority >= previous->second) {
            priorities[name] = priority;
            values[name] = value;
        }
    };
    auto apply = [&](const Declaration& declaration, int specificity) {
        std::string value = trim(declaration.value);
        size_t bang = value.rfind('!');
        bool important = bang != std::string::npos && trim(value.substr(bang + 1)) == "important";
        if (important) value = trim(value.substr(0, bang));
        int priority = specificity + (important ? 1000000 : 0);
        for (const auto& [name, part] : logical_to_physical(declaration.name, value))
            set(name, part, priority);
    };
    for (const auto& rule : stylesheet.rules) {
        for (const auto& selector : rule.selectors) {
            if (SelectorMatcher::matches_selector(node, selector, hovered_node, focused_node, active_node)) {
                for (const auto& decl : rule.declarations) {
                    apply(decl, SelectorMatcher::calculate_specificity(selector));
                }
            }
        }
    }
    auto inline_style = node->data.attributes.find("style");
    if (inline_style != node->data.attributes.end()) {
        CSSTokenizer tokenizer(inline_style->second);
        for (const auto& declaration : parse_declarations(tokenizer)) apply(declaration, 100000);
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
    if (tag == "ul" || tag == "ol") {
        for (auto outer = node->parent.lock(); outer; outer = outer->parent.lock()) {
            if (outer->tag_name == "ul" || outer->tag_name == "ol") {
                values["margin-top"] = "0px";
                values["margin-bottom"] = "0px";
                break;
            }
        }
    }
    if (tag == "input") {
        auto type = node->data.attributes.find("type");
        std::string input_type = type == node->data.attributes.end() ? "text" : type->second;
        if (input_type == "hidden") values["display"] = "none";
        if (input_type == "submit" || input_type == "button" || input_type == "reset") {
            resolve_default_values(values, "button");
            values["text-align"] = "center";
            values["box-sizing"] = "border-box";
        }
    }
    auto align = node->data.attributes.find("align");
    if (align != node->data.attributes.end()) values["text-align"] = align->second;
    auto bgcolor = node->data.attributes.find("bgcolor");
    if (bgcolor != node->data.attributes.end()) values["background-color"] = bgcolor->second;

    // form controls: defaults an author's CSS can override
    auto set_default = [&](const char* name, const char* value) {
        if (values.find(name) == values.end()) values[name] = value;
    };
    switch (forms::kind_of(*node)) {
        case forms::Kind::Text:
            set_default("display", "inline-block");
            set_default("background", "#ffffff");
            set_default("border", "1px solid #767676");
            set_default("border-radius", "3px");
            set_default("padding", "2px 3px");
            set_default("color", "#000000");
            set_default("font-size", "13.3px");
            set_default("margin", "0");
            break;
        case forms::Kind::Range:
        case forms::Kind::Progress:
            set_default("display", "inline-block");
            set_default("margin", "2px");
            break;
        case forms::Kind::Select:
            // the native look: a white box with a border and room on the right for the arrow
            set_default("display", "inline-block");
            set_default("background", "#ffffff");
            set_default("border", "1px solid #767676");
            set_default("border-radius", "3px");
            set_default("padding", "1px 16px 1px 4px");
            set_default("color", "#000000");
            set_default("font-size", "13.3px");
            set_default("margin", "0");
            break;
        case forms::Kind::None:
            break;
    }
    
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

    if (tag == "img" || tag == "video") {
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

    for (const auto& [name, value] : node->style_overrides) {
        values[name] = value;
    }
    
    return values;
}

void PropertyResolver::resolve_inherited_values(PropertyMap& values, const PropertyMap& inherited_values) {
    static const std::vector<std::string> inheritable_props = {
        "color", "font-family", "font-size", "font-weight", "line-height", "text-align", "opacity",
        "text-shadow", "white-space", "visibility", "fill", "text-transform", "letter-spacing"
    };

    for (const auto& prop : inheritable_props) {
        if (inherited_values.count(prop) && !values.count(prop)) {
            values[prop] = inherited_values.at(prop);
        }
    }
    for (const auto& pair : inherited_values) {
        if (pair.first.compare(0, 2, "--") == 0 && !values.count(pair.first)) values[pair.first] = pair.second;
    }
    for (auto& pair : values) {
        if (pair.first.compare(0, 2, "--") == 0) continue;
        std::string& value = pair.second;
        for (unsigned iteration = 0; iteration < 16; ++iteration) {
            size_t start = value.find("var(");
            if (start == std::string::npos) break;
            size_t end = start + 4, comma = std::string::npos;
            int depth = 1;
            for (; end < value.size(); ++end) {
                if (value[end] == '(') ++depth;
                else if (value[end] == ')' && --depth == 0) break;
                else if (value[end] == ',' && depth == 1 && comma == std::string::npos) comma = end;
            }
            if (end == value.size()) break;
            std::string name = trim(value.substr(start + 4, (comma == std::string::npos ? end : comma) - start - 4));
            auto variable = values.find(name);
            std::string replacement = variable != values.end() ? variable->second :
                comma != std::string::npos ? trim(value.substr(comma + 1, end - comma - 1)) : "";
            value.replace(start, end - start + 1, replacement);
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
