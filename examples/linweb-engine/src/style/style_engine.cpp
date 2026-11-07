#include <set>
#include "style/style_engine.h"
#include "style/selector_matcher.h"
#include "style/property_resolver.h"
#include "style/transform_parser.h"
#include "style/transition_engine.h"
#include "style/animation_engine.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>

namespace linweb {

std::map<Node*, std::map<std::string, TransitionState>> g_active_transitions;
std::map<Node*, std::map<std::string, AnimationState>> g_active_animations;
std::map<Node*, PropertyMap> g_last_specified_values;

namespace {

// What selector matching gave for an element, or for one of its pseudo-elements, in the last build that did the matching.
enum MatchKind { MatchSelf = 0, MatchBefore, MatchAfter, MatchFirstLine, MatchScrollbar };
struct MatchedStyle {
    uint64_t uid = 0;
    PropertyMap values;
};
std::map<std::pair<const Node*, int>, MatchedStyle> g_matched_styles;
bool g_reuse_matched_styles = false;
// the nodes a script changed since the last build: their matches are stale
std::unordered_set<uint64_t> g_stale_uids;       // the subtrees of the changed nodes
bool g_stale_all = false;

// The siblings of a changed node matter only to the selectors that read siblings (+, ~, :first-child, :nth-child,
// :empty...). The parts of those selectors say which siblings can be affected; the rest keep what they matched.
struct SiblingKey {
    std::string tag, id;
    std::vector<std::string> classes;
};
std::vector<SiblingKey> g_sibling_keys;
bool g_siblings_all = false;     // a selector these keys cannot describe (:not(), :is(), :has()...): every sibling counts
struct PendingScope {
    std::weak_ptr<Node> parent;
    uint64_t changed;
};
std::vector<PendingScope> g_pending_scopes;

void mark_stale(const Node* node) {
    g_stale_uids.insert(node->uid);
    for (const auto& child : node->children) mark_stale(child.get());
}

void add_sibling_key(const SimpleSelector& part) {
    g_sibling_keys.push_back({part.tag_name == "*" ? std::string() : part.tag_name, part.id, part.classes});
}

void read_sibling_keys(const StyleSheet& stylesheet) {
    g_sibling_keys.clear();
    g_siblings_all = false;
    for (const auto& rule : stylesheet.rules) {
        for (const auto& selector : rule.selectors) {
            bool sibling_combinator = false;
            for (Combinator combinator : selector.combinators) {
                if (combinator == Combinator::AdjacentSibling || combinator == Combinator::GeneralSibling) sibling_combinator = true;
            }
            for (const auto& part : selector.parts) {
                bool structural = false;
                for (const std::string& pseudo : part.pseudo_classes) {
                    if (pseudo.find("not(") != std::string::npos || pseudo.find("is(") != std::string::npos ||
                        pseudo.find("where(") != std::string::npos || pseudo.find("has(") != std::string::npos) g_siblings_all = true;
                    if (pseudo.find("child") != std::string::npos || pseudo.find("of-type") != std::string::npos ||
                        pseudo.find("empty") != std::string::npos || pseudo.find("nth") != std::string::npos) structural = true;
                }
                if (sibling_combinator || structural) add_sibling_key(part);
            }
        }
    }
}

bool matches_sibling_key(const Node* node) {
    if (node->type() != NodeType::Element) return false;
    const std::string id = node->data.id();
    const std::vector<std::string> classes = node->data.classes();
    for (const auto& key : g_sibling_keys) {
        if (!key.tag.empty() && key.tag != node->tag_name) continue;
        if (!key.id.empty() && key.id != id) continue;
        bool all_classes = true;
        for (const auto& cls : key.classes) {
            if (std::find(classes.begin(), classes.end(), cls) == classes.end()) { all_classes = false; break; }
        }
        if (all_classes) return true;
    }
    return false;
}

// Marks the siblings of the changed nodes that a sibling-reading selector can reach.
void mark_stale_siblings() {
    for (const auto& scope : g_pending_scopes) {
        const auto parent = scope.parent.lock();
        if (!parent) continue;
        for (const auto& child : parent->children) {
            if (child->uid == scope.changed) continue;
            if (g_siblings_all || matches_sibling_key(child.get())) mark_stale(child.get());
        }
    }
    g_pending_scopes.clear();
}

PropertyMap matched_styles(const std::shared_ptr<Node>& root, MatchKind kind, const std::shared_ptr<Node>& node,
                           const StyleSheet& stylesheet, const std::shared_ptr<Node>& hovered_node,
                           const std::shared_ptr<Node>& focused_node, const std::shared_ptr<Node>& active_node) {
    const auto key = std::make_pair(static_cast<const Node*>(root.get()), static_cast<int>(kind));
    if (g_reuse_matched_styles && !g_stale_all && !g_stale_uids.count(root->uid)) {
        auto found = g_matched_styles.find(key);
        if (found != g_matched_styles.end() && found->second.uid == root->uid) return found->second.values;
    }
    PropertyMap values = PropertyResolver::resolve_specified_values(node, stylesheet, hovered_node, focused_node, active_node);
    g_matched_styles[key] = MatchedStyle{root->uid, values};
    return values;
}

} // namespace

void StyleEngine::set_reuse_matched_styles(bool reuse) {
    // a build that matches again starts a cache of its own, so the nodes that left the page do not pile up
    if (!reuse) g_matched_styles.clear();
    g_reuse_matched_styles = reuse;
}

void StyleEngine::invalidate_matched_subtree(const Node* scope) {
    if (!scope) {
        g_stale_all = true;
        return;
    }
    mark_stale(scope);
    // its siblings are marked when the build knows which of them the stylesheet reads
    if (const auto parent = scope->parent.lock()) g_pending_scopes.push_back({parent, scope->uid});
}

void StyleEngine::invalidate_matched_all() {
    g_stale_all = true;
}

bool StyleEngine::matched_scoped() {
    return !g_stale_all;
}

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
    if (g_reuse_matched_styles && !g_pending_scopes.empty()) {
        read_sibling_keys(stylesheet);
        mark_stale_siblings();
    }
    auto tree = build_style_tree_recursive(root, stylesheet, hovered_node, focused_node, active_node,
                                           current_time, parent_width, parent_height, inherited_values);
    // every stale node was matched again by this build
    g_stale_uids.clear();
    g_pending_scopes.clear();
    g_stale_all = false;
    return tree;
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

    // A node created after another was freed can get its address. What was kept for that address (the last style, the
    // running transitions and animations) belongs to the old node, so it must not play on the new one.
    {
        static std::map<Node*, uint64_t> owner_of;
        auto owner = owner_of.find(root.get());
        if (owner != owner_of.end() && owner->second != root->uid) {
            g_last_specified_values.erase(root.get());
            g_active_transitions.erase(root.get());
            g_active_animations.erase(root.get());
        }
        owner_of[root.get()] = root->uid;
    }

    styled_node->specified_values = matched_styles(root, MatchSelf, root, stylesheet, hovered_node, focused_node, active_node);

    PropertyResolver::resolve_inherited_values(styled_node->specified_values, inherited_values);
    // Resolve relative font sizes once against the parent's computed size.
    // Descendant layout must not repeatedly interpret the same percentage.
    std::string font = styled_node->value("font-size");
    if (!font.empty()) {
        float parent_font = inherited_values.count("font-size") ? parse_px(inherited_values.at("font-size"), 16) : 16;
        if (parent_font <= 0) parent_font = 16;
        float pixels = 0;
        try {
            if (font == "small") pixels = 13;
            else if (font == "medium") pixels = 16;
            else if (font == "large") pixels = 18;
            else if (font.find("rem") != std::string::npos) pixels = std::stof(font) * 16;
            else if (font.find("em") != std::string::npos) pixels = std::stof(font) * parent_font;
            else if (font.find('%') != std::string::npos) pixels = std::stof(font) * parent_font / 100;
            else if (font.find("pt") != std::string::npos) pixels = std::stof(font) * 96 / 72;
        } catch (...) {}
        if (pixels > 0) styled_node->specified_values["font-size"] = std::to_string(pixels) + "px";
    }
    PropertyResolver::resolve_currentColor(styled_node->specified_values, inherited_values);
    
    TransitionEngine::apply_transitions(styled_node, current_time, parent_width, parent_height);
    AnimationEngine::apply_animations(styled_node, stylesheet, current_time);
    
    // an inherited property a transition or animation is playing reaches the children as its current value
    for (const char* name : {"font-size", "color", "text-shadow"}) {
        auto animated = styled_node->animated_values.find(name);
        if (animated != styled_node->animated_values.end()) styled_node->specified_values[name] = animated->second;
    }

    std::string transform_str = styled_node->value("transform");
    if (!transform_str.empty()) {
        styled_node->transform = TransformParser::parse_transform(transform_str);
        
        std::string origin_str = styled_node->value("transform-origin");
        if (!origin_str.empty()) {
            TransformParser::parse_transform_origin(origin_str, styled_node->transform);
        }
    }
    TransformParser::apply_translate_property(styled_node->value("translate"), styled_node->transform);
    
    for (const auto& child : root->children) {
        styled_node->children.push_back(build_style_tree_recursive(
            child, stylesheet, hovered_node, focused_node, active_node,
            current_time, parent_width, parent_height, styled_node->specified_values));
    }

    // ::first-line styles the first line of text. Approximated by the first text run of the element (up to a <br>),
    // which covers a title followed by line breaks; only the properties a line of text can take are applied.
    // ::-webkit-scrollbar { display: none } hides the scrollbars of the element it styles, as scrollbar-width: none does
    if (stylesheet.has_scrollbar_rules && root->is_element() && root->tag_name.rfind("::", 0) != 0 &&
        !styled_node->specified_values.count("scrollbar-width")) {
        auto scrollbar_node = std::make_shared<PseudoElementNode>("-webkit-scrollbar", "", root);
        scrollbar_node->parent = root;
        PropertyMap scrollbar = matched_styles(root, MatchScrollbar, scrollbar_node, stylesheet, hovered_node, focused_node, active_node);
        auto display = scrollbar.find("display");
        if (display != scrollbar.end() && display->second == "none") styled_node->specified_values["scrollbar-width"] = "none";
    }

    if (stylesheet.has_first_line_rules && root->is_element() && root->tag_name.rfind("::", 0) != 0) {
        auto first_line_node = std::make_shared<PseudoElementNode>("first-line", "", root);
        first_line_node->parent = root;
        PropertyMap first_line = matched_styles(root, MatchFirstLine, first_line_node, stylesheet, hovered_node, focused_node, active_node);
        static const char* const line_properties[] = {"font-size", "font-weight", "font-style", "font-family", "color",
            "letter-spacing", "text-transform", "text-shadow", "line-height", "opacity"};
        for (const auto& child : styled_node->children) {
            auto text = std::dynamic_pointer_cast<TextNode>(child->node);
            if (!text || text->data.find_first_not_of(" \t\n\r\f") == std::string::npos) continue;
            for (const char* name : line_properties) {
                auto it = first_line.find(name);
                if (it != first_line.end()) child->specified_values[name] = it->second;
            }
            break;
        }
    }

    if (root->is_element() && root->tag_name != "::before" && root->tag_name != "::after") {
        auto create_pseudo = [&](const std::string& pseudo_type) -> std::shared_ptr<StyledNode> {
            auto pseudo_node = std::make_shared<PseudoElementNode>(pseudo_type, "", root);
            pseudo_node->parent = root;
            auto pseudo_styled = std::make_shared<StyledNode>(pseudo_node);
            pseudo_styled->specified_values = matched_styles(root, pseudo_type == "before" ? MatchBefore : MatchAfter, pseudo_node, stylesheet, hovered_node, focused_node, active_node);
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

bool StyleEngine::has_running_animations() {
    for (const auto& node_entry : g_active_transitions) {
        if (!node_entry.second.empty()) return true;
    }
    for (const auto& node_entry : g_active_animations) {
        for (const auto& anim_entry : node_entry.second) {
            if (!anim_entry.second.finished) return true;
        }
    }
    return false;
}

bool StyleEngine::has_layout_affecting_animations() {
    // paint-only properties are re-applied on the existing style tree every frame; the rest needs a rebuild
    static const std::set<std::string> paint_only = {
        "transform", "opacity", "filter", "color", "background-color", "background", "box-shadow", "text-shadow"};
    for (const auto& node_entry : g_active_transitions) {
        for (const auto& prop_entry : node_entry.second) {
            if (!paint_only.count(prop_entry.first)) return true;
        }
    }
    // a finished animation keeps its last frame in the style tree; only running ones need another rebuild
    for (const auto& node_entry : g_active_animations) {
        for (const auto& anim_entry : node_entry.second) {
            if (!anim_entry.second.finished && anim_entry.second.layout_affecting) return true;
        }
    }
    return false;
}

} // namespace linweb
