#include "style/style_engine.h"
#include "style/selector_matcher.h"
#include "style/property_resolver.h"
#include "style/transform_parser.h"
#include "style/transition_engine.h"
#include "style/animation_engine.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace linweb {

std::map<Node*, std::map<std::string, TransitionState>> g_active_transitions;
std::map<Node*, std::map<std::string, AnimationState>> g_active_animations;
std::map<Node*, PropertyMap> g_last_specified_values;

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float ease(float t) {
    return t * t * (3.0f - 2.0f * t);
}

float ease_in(float t) {
    return t * t;
}

float ease_out(float t) {
    return t * (2.0f - t);
}

float ease_in_out(float t) {
    return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
}

float cubic_bezier(float t, float x1, float y1, float x2, float y2) {
    float u = t;
    for (int i = 0; i < 12; i++) {
        float x = 3.0f * (1.0f - u) * (1.0f - u) * u * x1 + 3.0f * (1.0f - u) * u * u * x2 + u * u * u;
        float dx = 3.0f * (1.0f - u) * (1.0f - u) * x1 + 6.0f * (1.0f - u) * u * (x2 - x1) + 3.0f * u * u * (1.0f - x2);
        if (std::fabs(dx) < 1e-6f) break;
        u = u - (x - t) / dx;
    }
    u = std::max(0.0f, std::min(1.0f, u));
    return 3.0f * (1.0f - u) * (1.0f - u) * u * y1 + 3.0f * (1.0f - u) * u * u * y2 + u * u * u;
}

float parse_px(const std::string& s, float reference_val) {
    try {
        std::string trimmed = trim(s);
        if (trimmed.empty()) return 0;
        if (trimmed == "auto") return reference_val;
        if (trimmed.back() == '%') {
            float percent = std::stof(trimmed.substr(0, trimmed.size() - 1));
            return (percent / 100.0f) * reference_val;
        }
        size_t px = trimmed.find("px");
        if (px != std::string::npos) return std::stof(trimmed.substr(0, px));
        size_t rem = trimmed.find("rem");
        if (rem != std::string::npos) return std::stof(trimmed.substr(0, rem)) * 16.0f;
        return std::stof(trimmed);
    } catch(...) { return 0; }
}

std::string format_value(float val, const std::string& original) {
    if (!original.empty() && original.back() == '%') return std::to_string(val) + "%";
    return std::to_string(val) + "px";
}

DropShadowValue parse_drop_shadow(const std::string& s) {
    DropShadowValue ds;
    if (s == "none" || s.empty()) return ds;
    
    size_t start = s.find("drop-shadow(");
    if (start == std::string::npos) return ds;
    
    std::string inner = s.substr(start + 12);
    if (!inner.empty() && inner.back() == ')') inner.pop_back();
    
    size_t color_start = inner.find("rgba(");
    if (color_start == std::string::npos) color_start = inner.find("rgb(");
    if (color_start == std::string::npos) color_start = inner.find("#");
    
    std::string dims_part;
    std::string color_part;
    
    if (color_start != std::string::npos) {
        dims_part = inner.substr(0, color_start);
        color_part = inner.substr(color_start);
    } else {
        size_t last_space = inner.find_last_of(' ');
        if (last_space != std::string::npos) {
            std::string last_word = inner.substr(last_space + 1);
            if (last_word.find("px") == std::string::npos && last_word.find("%") == std::string::npos && 
                last_word != "0" && !std::isdigit(last_word[0])) {
                dims_part = inner.substr(0, last_space);
                color_part = last_word;
            } else {
                dims_part = inner;
            }
        } else {
            dims_part = inner;
        }
    }
    
    std::stringstream ss(dims_part);
    std::string val;
    std::vector<float> dims;
    while (ss >> val) {
        dims.push_back(parse_px(val));
    }
    
    if (dims.size() >= 1) ds.x = dims[0];
    if (dims.size() >= 2) ds.y = dims[1];
    if (dims.size() >= 3) ds.blur = dims[2];
    
    if (!color_part.empty()) ds.color = parse_color_str(color_part);
    
    return ds;
}

std::shared_ptr<StyledNode> StyleEngine::build_style_tree(
    const std::shared_ptr<Node>& root,
    const StyleSheet& stylesheet,
    const std::shared_ptr<Node>& hovered_node,
    const std::shared_ptr<Node>& focused_node,
    const std::shared_ptr<Node>& active_node,
    double current_time,
    float parent_width,
    float parent_height,
    PropertyMap inherited_values) {
    return build_style_tree_recursive(root, stylesheet, hovered_node, focused_node, active_node,
                                      current_time, parent_width, parent_height, inherited_values);
}

std::shared_ptr<StyledNode> StyleEngine::build_style_tree_recursive(
    const std::shared_ptr<Node>& root,
    const StyleSheet& stylesheet,
    const std::shared_ptr<Node>& hovered_node,
    const std::shared_ptr<Node>& focused_node,
    const std::shared_ptr<Node>& active_node,
    double current_time,
    float parent_width,
    float parent_height,
    const PropertyMap& inherited_values) {
    auto styled_node = std::make_shared<StyledNode>(root);
    
    styled_node->specified_values = PropertyResolver::resolve_specified_values(
        root, stylesheet, hovered_node, focused_node, active_node);
    
    PropertyResolver::resolve_inherited_values(styled_node->specified_values, inherited_values);
    PropertyResolver::resolve_currentColor(styled_node->specified_values, inherited_values);
    
    TransitionEngine::apply_transitions(styled_node, current_time, parent_width, parent_height);
    AnimationEngine::apply_animations(styled_node, stylesheet, current_time);
    
    std::string transform_str = styled_node->value("transform");
    if (!transform_str.empty()) {
        styled_node->transform = TransformParser::parse_transform(transform_str);
        
        std::string origin_str = styled_node->value("transform-origin");
        if (!origin_str.empty()) {
            TransformParser::parse_transform_origin(origin_str, styled_node->transform);
        }
    }
    
    for (const auto& child : root->children) {
        styled_node->children.push_back(build_style_tree_recursive(
            child, stylesheet, hovered_node, focused_node, active_node,
            current_time, parent_width, parent_height, styled_node->specified_values));
    }

    if (root->is_element() && root->tag_name != "::before" && root->tag_name != "::after") {
        auto create_pseudo = [&](const std::string& pseudo_type) -> std::shared_ptr<StyledNode> {
            auto pseudo_node = std::make_shared<PseudoElementNode>(pseudo_type, "", root);
            pseudo_node->parent = root;
            auto pseudo_styled = std::make_shared<StyledNode>(pseudo_node);
            pseudo_styled->specified_values = PropertyResolver::resolve_specified_values(
                pseudo_node, stylesheet, hovered_node, focused_node, active_node);
            PropertyResolver::resolve_inherited_values(pseudo_styled->specified_values, styled_node->specified_values);
            PropertyResolver::resolve_currentColor(pseudo_styled->specified_values, styled_node->specified_values);

            std::string content_val = pseudo_styled->value("content");
            if (!content_val.empty()) {
                std::string parsed_content;
                bool in_quotes = false;
                char quote_char = 0;
                for (size_t i = 0; i < content_val.size(); ++i) {
                    char c = content_val[i];
                    if (!in_quotes && (c == '\"' || c == '\'')) {
                        in_quotes = true;
                        quote_char = c;
                    } else if (in_quotes && c == quote_char) {
                        in_quotes = false;
                    } else if (in_quotes) {
                        parsed_content += c;
                    }
                }
                pseudo_node->pseudo_content = parsed_content;
                pseudo_styled->specified_values["display"] = pseudo_styled->specified_values.count("display") ? pseudo_styled->specified_values["display"] : "inline";
                return pseudo_styled;
            }
            return nullptr;
        };

        auto before = create_pseudo("before");
        if (before) {
            styled_node->children.insert(styled_node->children.begin(), before);
        }

        auto after = create_pseudo("after");
        if (after) {
            styled_node->children.push_back(after);
        }
    }
    
    return styled_node;
}

bool StyleEngine::matches(
    const std::shared_ptr<Node>& node,
    const Selector& selector,
    const std::shared_ptr<Node>& hovered_node,
    const std::shared_ptr<Node>& focused_node,
    const std::shared_ptr<Node>& active_node) {
    return SelectorMatcher::matches_selector(node, selector, hovered_node, focused_node, active_node);
}

bool StyleEngine::has_layout_affecting_animations() {
    auto node_has_will_change_transform = [](Node* node) -> bool {
        auto it = g_last_specified_values.find(node);
        if (it != g_last_specified_values.end()) {
            auto wc_it = it->second.find("will-change");
            if (wc_it != it->second.end()) {
                return wc_it->second.find("transform") != std::string::npos;
            }
        }
        return false;
    };

    for (const auto& node_entry : g_active_transitions) {
        for (const auto& prop_entry : node_entry.second) {
            const std::string& prop = prop_entry.first;
            if (prop != "transform" && prop != "opacity") {
                return true;
            }
            if (!node_has_will_change_transform(node_entry.first)) {
                return true;
            }
        }
    }
    if (!g_active_animations.empty()) {
        return true;
    }
    return false;
}

} // namespace linweb
