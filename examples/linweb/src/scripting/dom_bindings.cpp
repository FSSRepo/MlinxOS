#include "scripting/dom_bindings.h"
#include "scripting/js_engine.h"
#include "core/dom.h"
#include "layout/layout_engine.h"
#include "parser/html/html_parser.h"
#include "parser/css/css_parser.h"
#include "style/style_engine.h"
#include "utils/dom_traversal.h"
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <functional>

namespace linweb {

extern std::shared_ptr<Node> g_dom_root;
extern std::shared_ptr<StyledNode> g_style_tree;
extern std::shared_ptr<LayoutBox> g_layout_tree;
extern std::shared_ptr<StyledNode> g_hovered_node;
extern std::shared_ptr<Node> g_focused_node;
extern std::shared_ptr<Node> g_active_node;
extern std::vector<std::shared_ptr<Node>> g_detached_nodes;
void mark_dom_dirty(const Node* node);
extern class JSEngine* g_js_engine;

// The shared node behind an address a wrapper holds. A subtree a script is still building is not in the document and
// can be several levels deep, so every detached root is searched too.
std::shared_ptr<Node> find_dom_node_by_address(Node* ptr) {
    if (!ptr) return nullptr;

    std::function<std::shared_ptr<Node>(const std::shared_ptr<Node>&)> find_in_tree;
    find_in_tree = [&](const std::shared_ptr<Node>& node) -> std::shared_ptr<Node> {
        if (!node) return nullptr;
        if (node.get() == ptr) return node;
        for (const auto& child : node->children) {
            if (auto found = find_in_tree(child)) return found;
        }
        return nullptr;
    };

    if (auto found = find_in_tree(g_dom_root)) return found;
    for (const auto& root : g_detached_nodes) {
        if (auto found = find_in_tree(root)) return found;
    }
    return nullptr;
}

std::string get_text_content_recursive(const std::shared_ptr<Node>& node) {
    if (node->type() == NodeType::Text) {
        return std::static_pointer_cast<TextNode>(node)->data;
    }
    std::string result;
    for (const auto& child : node->children) {
        result += get_text_content_recursive(child);
    }
    return result;
}

std::string get_inner_html_recursive(const std::shared_ptr<Node>& node) {
    std::string result;
    for (const auto& child : node->children) {
        if (child->type() == NodeType::Text) {
            result += std::static_pointer_cast<TextNode>(child)->data;
        } else if (child->type() == NodeType::Element) {
            auto el = std::static_pointer_cast<ElementNode>(child);
            result += "<" + el->tag_name;
            for (const auto& attr : el->data.attributes) {
                result += " " + attr.first + "=\"" + attr.second + "\"";
            }
            result += ">";
            result += get_inner_html_recursive(child);
            result += "</" + el->tag_name + ">";
        }
    }
    return result;
}

JSValue js_get_element_by_id(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NULL;
    const char* id_str = JS_ToCString(ctx, argv[0]);
    if (!id_str) return JS_NULL;
    std::string id(id_str);
    JS_FreeCString(ctx, id_str);

    std::shared_ptr<Node> found;
    if (g_dom_root) {
        found = find_node_by_id_recursive(g_dom_root, id);
    }
    if (!found) return JS_NULL;

    JSValue el_obj = JS_NewObject(ctx);
    int64_t addr = reinterpret_cast<int64_t>(found.get());
    JS_SetPropertyStr(ctx, el_obj, "_addr", JS_NewBigInt64(ctx, addr));
    if (found->type() == NodeType::Element) {
        auto el = std::static_pointer_cast<ElementNode>(found);
        JS_SetPropertyStr(ctx, el_obj, "tagName", JS_NewString(ctx, el->tag_name.c_str()));
    }
    return el_obj;
}

JSValue js_query_selector_all(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_UNDEFINED;

    const char* selector_str = JS_ToCString(ctx, argv[0]);
    if (!selector_str) return JS_UNDEFINED;

    std::vector<Selector> selectors = CSSParser::parse_selectors(selector_str);
    JS_FreeCString(ctx, selector_str);

    std::vector<std::shared_ptr<Node>> found_nodes;
    if (g_dom_root) {
        find_nodes_by_selector(g_dom_root, selectors, found_nodes);
    }

    JSValue arr = JS_NewArray(ctx);
    for (size_t i = 0; i < found_nodes.size(); ++i) {
        auto node = found_nodes[i];

        JSValue el_obj = JS_NewObject(ctx);

        int64_t addr = reinterpret_cast<int64_t>(node.get());
        JS_SetPropertyStr(ctx, el_obj, "_addr", JS_NewBigInt64(ctx, addr));

        if (node->type() == NodeType::Element) {
            auto el = std::static_pointer_cast<ElementNode>(node);
            JS_SetPropertyStr(ctx, el_obj, "tagName", JS_NewString(ctx, el->tag_name.c_str()));
        }

        JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i), el_obj);
    }

    return arr;
}

JSValue js_query_selector_all_by_addr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;

    int64_t addr_int;
    if (JS_ToInt64Ext(ctx, &addr_int, argv[0])) return JS_UNDEFINED;

    const char* selector_str = JS_ToCString(ctx, argv[1]);
    if (!selector_str) return JS_UNDEFINED;

    std::vector<Selector> selectors = CSSParser::parse_selectors(selector_str);
    JS_FreeCString(ctx, selector_str);

    std::vector<std::shared_ptr<Node>> found_nodes;
    Node* root_ptr = reinterpret_cast<Node*>(addr_int);
    auto root_shared = find_dom_node_by_address(root_ptr);
    if (root_shared) {
        find_nodes_by_selector(root_shared, selectors, found_nodes);
    }

    JSValue arr = JS_NewArray(ctx);
    for (size_t i = 0; i < found_nodes.size(); ++i) {
        auto node = found_nodes[i];

        JSValue el_obj = JS_NewObject(ctx);

        int64_t addr = reinterpret_cast<int64_t>(node.get());
        JS_SetPropertyStr(ctx, el_obj, "_addr", JS_NewBigInt64(ctx, addr));

        if (node->type() == NodeType::Element) {
            auto el = std::static_pointer_cast<ElementNode>(node);
            JS_SetPropertyStr(ctx, el_obj, "tagName", JS_NewString(ctx, el->tag_name.c_str()));
        }

        JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i), el_obj);
    }

    return arr;
}

JSValue js_create_element(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_UNDEFINED;

    const char* tag_name = JS_ToCString(ctx, argv[0]);
    if (!tag_name) return JS_UNDEFINED;

    auto node = std::make_shared<ElementNode>(tag_name, std::unordered_map<std::string, std::string>());
    JS_FreeCString(ctx, tag_name);

    g_detached_nodes.push_back(node);

    JSValue el_obj = JS_NewObject(ctx);
    int64_t addr = reinterpret_cast<int64_t>(node.get());
    JS_SetPropertyStr(ctx, el_obj, "_addr", JS_NewBigInt64(ctx, addr));
    JS_SetPropertyStr(ctx, el_obj, "tagName", JS_NewString(ctx, node->tag_name.c_str()));

    return el_obj;
}

// A plain object that document._wrap() turns into an element wrapper: the native address and, for elements, the tag.
static JSValue make_node_ref(JSContext* ctx, const std::shared_ptr<Node>& node) {
    JSValue obj = JS_NewObject(ctx);
    if (!node) return obj;
    int64_t addr = reinterpret_cast<int64_t>(node.get());
    JS_SetPropertyStr(ctx, obj, "_addr", JS_NewBigInt64(ctx, addr));
    if (node->type() == NodeType::Element) {
        JS_SetPropertyStr(ctx, obj, "tagName", JS_NewString(ctx, node->tag_name.c_str()));
    }
    return obj;
}

// The native pointer held by a JS argument that is an element address, or nullptr.
static Node* node_address_arg(JSContext* ctx, JSValueConst value) {
    int64_t addr = 0;
    if (JS_ToInt64Ext(ctx, &addr, value)) {
        // an argument that is not an address: drop the pending conversion error, the caller treats it as "no node"
        JSValue exception = JS_GetException(ctx);
        JS_FreeValue(ctx, exception);
        return nullptr;
    }
    return reinterpret_cast<Node*>(addr);
}

static void detach_from_parent(const std::shared_ptr<Node>& node) {
    if (auto old_parent = node->parent.lock()) {
        auto& siblings = old_parent->children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), node), siblings.end());
    }
    node->parent.reset();
}

// A node that was attached to the document or to another detached tree may still be named by JS: keep only its root.
static void forget_detached_root(const std::shared_ptr<Node>& node) {
    g_detached_nodes.erase(std::remove(g_detached_nodes.begin(), g_detached_nodes.end(), node), g_detached_nodes.end());
}

static bool is_ancestor_of(const std::shared_ptr<Node>& ancestor, const std::shared_ptr<Node>& node) {
    for (auto parent = node->parent.lock(); parent; parent = parent->parent.lock()) {
        if (parent == ancestor) return true;
    }
    return false;
}

// Moves `child` under `parent`, before `reference` when it is one of its children, at the end otherwise.
static void attach_child(const std::shared_ptr<Node>& parent, const std::shared_ptr<Node>& child,
                         const std::shared_ptr<Node>& reference) {
    const auto old_parent = child->parent.lock();
    detach_from_parent(child);
    forget_detached_root(child);

    auto& children = parent->children;
    if (reference) {
        auto it = std::find(children.begin(), children.end(), reference);
        if (it != children.end()) children.insert(it, child);
        else children.push_back(child);
    } else {
        children.push_back(child);
    }
    child->parent = parent;
    mark_dom_dirty(parent.get());
    if (old_parent) mark_dom_dirty(old_parent.get());
}

JSValue js_append_child(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;

    auto parent = find_dom_node_by_address(node_address_arg(ctx, argv[0]));
    auto child = find_dom_node_by_address(node_address_arg(ctx, argv[1]));
    if (!parent || !child || parent == child || is_ancestor_of(child, parent)) return JS_UNDEFINED;

    attach_child(parent, child, nullptr);
    return JS_UNDEFINED;
}

JSValue js_insert_before(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_UNDEFINED;

    auto parent = find_dom_node_by_address(node_address_arg(ctx, argv[0]));
    auto child = find_dom_node_by_address(node_address_arg(ctx, argv[1]));
    if (!parent || !child || parent == child || is_ancestor_of(child, parent)) return JS_UNDEFINED;

    auto reference = find_dom_node_by_address(node_address_arg(ctx, argv[2]));
    if (reference && reference->parent.lock() != parent) reference = nullptr;
    if (reference == child) return JS_UNDEFINED; // already in that place

    attach_child(parent, child, reference);
    return JS_UNDEFINED;
}

JSValue js_remove_node(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_UNDEFINED;

    auto node = find_dom_node_by_address(node_address_arg(ctx, argv[0]));
    if (!node || !node->parent.lock()) return JS_UNDEFINED;

    const auto old_parent = node->parent.lock();
    detach_from_parent(node);
    // the JS wrapper only holds the address, so the detached subtree has to outlive the DOM tree
    g_detached_nodes.push_back(node);
    mark_dom_dirty(old_parent.get());
    return JS_UNDEFINED;
}

JSValue js_remove_child(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;

    auto parent = find_dom_node_by_address(node_address_arg(ctx, argv[0]));
    auto child = find_dom_node_by_address(node_address_arg(ctx, argv[1]));
    if (!parent || !child || child->parent.lock() != parent) return JS_UNDEFINED;

    detach_from_parent(child);
    g_detached_nodes.push_back(child);
    mark_dom_dirty(parent.get());
    return JS_UNDEFINED;
}

// The element children, in document order, as raw node references for document._wrap().
JSValue js_get_element_children(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSValue arr = JS_NewArray(ctx);
    if (argc < 1) return arr;

    auto node = find_dom_node_by_address(node_address_arg(ctx, argv[0]));
    if (!node) return arr;

    uint32_t index = 0;
    for (const auto& child : node->children) {
        if (child->type() != NodeType::Element) continue;
        JS_SetPropertyUint32(ctx, arr, index++, make_node_ref(ctx, child));
    }
    return arr;
}

JSValue js_get_parent_element(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NULL;

    auto node = find_dom_node_by_address(node_address_arg(ctx, argv[0]));
    if (!node) return JS_NULL;

    auto parent = node->parent.lock();
    // the parser's synthetic root is above <html>; a browser has no element there
    if (!parent || parent->type() != NodeType::Element || parent->tag_name == "root") return JS_NULL;
    return make_node_ref(ctx, parent);
}

static JSValue element_sibling(JSContext* ctx, Node* raw, int direction) {
    auto node = find_dom_node_by_address(raw);
    if (!node) return JS_NULL;

    auto parent = node->parent.lock();
    if (!parent) return JS_NULL;

    auto& children = parent->children;
    auto it = std::find(children.begin(), children.end(), node);
    if (it == children.end()) return JS_NULL;

    for (;;) {
        if (direction > 0) {
            ++it;
            if (it == children.end()) return JS_NULL;
        } else {
            if (it == children.begin()) return JS_NULL;
            --it;
        }
        if ((*it)->type() == NodeType::Element) return make_node_ref(ctx, *it);
    }
}

JSValue js_get_next_element_sibling(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NULL;
    return element_sibling(ctx, node_address_arg(ctx, argv[0]), 1);
}

JSValue js_get_previous_element_sibling(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NULL;
    return element_sibling(ctx, node_address_arg(ctx, argv[0]), -1);
}

JSValue js_set_text_content(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;

    int64_t addr_int;
    if (JS_ToInt64Ext(ctx, &addr_int, argv[0])) return JS_UNDEFINED;

    const char* text = JS_ToCString(ctx, argv[1]);
    if (!text) return JS_UNDEFINED;

    Node* node_ptr = reinterpret_cast<Node*>(addr_int);
    if (node_ptr) {
        node_ptr->children.clear();
        auto parent_shared = find_dom_node_by_address(node_ptr);
        auto add_child = [&](const std::shared_ptr<Node>& child) {
            node_ptr->children.push_back(child);
            if (parent_shared) child->parent = parent_shared;
        };

        // innerText turns each line break into a <br>, as a browser does; textContent keeps the text as one node
        bool as_lines = argc > 2 && JS_ToBool(ctx, argv[2]);
        std::string value = text;
        if (as_lines && value.find('\n') != std::string::npos) {
            size_t start = 0;
            while (true) {
                size_t end = value.find('\n', start);
                std::string line = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) add_child(std::make_shared<TextNode>(line));
                if (end == std::string::npos) break;
                add_child(std::make_shared<ElementNode>("br", std::unordered_map<std::string, std::string>{}));
                start = end + 1;
            }
        } else {
            add_child(std::make_shared<TextNode>(value));
        }
        mark_dom_dirty(node_ptr);
    }

    JS_FreeCString(ctx, text);
    return JS_UNDEFINED;
}

JSValue js_get_text_content(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_UNDEFINED;
    int64_t addr_int;
    if (JS_ToInt64Ext(ctx, &addr_int, argv[0])) return JS_UNDEFINED;

    Node* node_ptr = reinterpret_cast<Node*>(addr_int);
    if (node_ptr) {
        auto shared = find_dom_node_by_address(node_ptr);
        if (shared) {
            return JS_NewString(ctx, get_text_content_recursive(shared).c_str());
        }
    }
    return JS_NewString(ctx, "");
}

JSValue js_set_inner_html(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;
    int64_t addr_int;
    if (JS_ToInt64Ext(ctx, &addr_int, argv[0])) return JS_UNDEFINED;
    const char* html_str = JS_ToCString(ctx, argv[1]);
    if (!html_str) return JS_UNDEFINED;

    Node* node_ptr = reinterpret_cast<Node*>(addr_int);
    if (node_ptr) {
        node_ptr->children.clear();
        std::string html(html_str);
        if (!html.empty()) {
            auto fragment = HTMLParser::parse(html);
            if (fragment) {
                auto parent_shared = find_dom_node_by_address(node_ptr);
                if (fragment->tag_name == "root") {
                    for (auto& child : fragment->children) {
                        node_ptr->children.push_back(child);
                        if (parent_shared) child->parent = parent_shared;
                    }
                } else {
                    node_ptr->children.push_back(fragment);
                    if (parent_shared) fragment->parent = parent_shared;
                }
            }
        }
        mark_dom_dirty(node_ptr);
    }
    JS_FreeCString(ctx, html_str);
    return JS_UNDEFINED;
}

JSValue js_get_inner_html(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_UNDEFINED;
    int64_t addr_int;
    if (JS_ToInt64Ext(ctx, &addr_int, argv[0])) return JS_UNDEFINED;

    Node* node_ptr = reinterpret_cast<Node*>(addr_int);
    if (node_ptr) {
        auto shared = find_dom_node_by_address(node_ptr);
        if (shared) {
            return JS_NewString(ctx, get_inner_html_recursive(shared).c_str());
        }
    }
    return JS_NewString(ctx, "");
}

JSValue js_get_class_name(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NewString(ctx, "");

    int64_t addr_int;
    if (JS_ToInt64Ext(ctx, &addr_int, argv[0])) return JS_NewString(ctx, "");

    Node* node_ptr = reinterpret_cast<Node*>(addr_int);
    if (node_ptr && node_ptr->type() == NodeType::Element) {
        auto element = static_cast<ElementNode*>(node_ptr);
        auto it = element->data.attributes.find("class");
        if (it != element->data.attributes.end()) {
            return JS_NewString(ctx, it->second.c_str());
        }
    }
    return JS_NewString(ctx, "");
}

JSValue js_set_class_name(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;

    int64_t addr_int;
    if (JS_ToInt64Ext(ctx, &addr_int, argv[0])) return JS_UNDEFINED;

    const char* class_str = JS_ToCString(ctx, argv[1]);
    if (!class_str) return JS_UNDEFINED;

    Node* node_ptr = reinterpret_cast<Node*>(addr_int);
    if (node_ptr && node_ptr->type() == NodeType::Element) {
        auto element = static_cast<ElementNode*>(node_ptr);
        element->data.attributes["class"] = class_str;
        mark_dom_dirty(element);
    }

    JS_FreeCString(ctx, class_str);
    return JS_UNDEFINED;
}

JSValue js_get_attribute(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_NULL;

    int64_t addr_int;
    if (JS_ToInt64Ext(ctx, &addr_int, argv[0])) return JS_NULL;

    const char* attr_name = JS_ToCString(ctx, argv[1]);
    if (!attr_name) return JS_NULL;
    std::string attr(attr_name);
    JS_FreeCString(ctx, attr_name);

    Node* node_ptr = reinterpret_cast<Node*>(addr_int);
    if (node_ptr && node_ptr->type() == NodeType::Element) {
        auto element = static_cast<ElementNode*>(node_ptr);
        auto it = element->data.attributes.find(attr);
        if (it != element->data.attributes.end()) {
            return JS_NewString(ctx, it->second.c_str());
        }
    }
    return JS_NULL;
}

JSValue js_add_class_by_addr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;

    int64_t addr_int;
    if (JS_ToInt64Ext(ctx, &addr_int, argv[0])) return JS_UNDEFINED;

    Node* node_ptr = reinterpret_cast<Node*>(addr_int);
    if (node_ptr && node_ptr->type() == NodeType::Element) {
        auto element = static_cast<ElementNode*>(node_ptr);
        auto classes = element->data.classes();

        bool changed = false;
        for (int i = 1; i < argc; i++) {
            const char* cls_to_add = JS_ToCString(ctx, argv[i]);
            if (cls_to_add) {
                if (std::find(classes.begin(), classes.end(), std::string(cls_to_add)) == classes.end()) {
                    classes.push_back(cls_to_add);
                    changed = true;
                }
                JS_FreeCString(ctx, cls_to_add);
            }
        }

        if (changed) {
            std::string updated;
            for (size_t i = 0; i < classes.size(); ++i) {
                updated += classes[i] + (i == classes.size() - 1 ? "" : " ");
            }
            element->data.attributes["class"] = updated;
            mark_dom_dirty(element);
        }
    }

    return JS_UNDEFINED;
}

JSValue js_remove_class_by_addr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;

    int64_t addr_int;
    if (JS_ToInt64Ext(ctx, &addr_int, argv[0])) return JS_UNDEFINED;

    Node* node_ptr = reinterpret_cast<Node*>(addr_int);
    if (node_ptr && node_ptr->type() == NodeType::Element) {
        auto element = static_cast<ElementNode*>(node_ptr);
        auto classes = element->data.classes();

        bool changed = false;
        for (int i = 1; i < argc; i++) {
            const char* cls_to_remove = JS_ToCString(ctx, argv[i]);
            if (cls_to_remove) {
                auto it = std::find(classes.begin(), classes.end(), std::string(cls_to_remove));
                if (it != classes.end()) {
                    classes.erase(it);
                    changed = true;
                }
                JS_FreeCString(ctx, cls_to_remove);
            }
        }

        if (changed) {
            std::string updated;
            for (size_t i = 0; i < classes.size(); ++i) {
                updated += classes[i] + (i == classes.size() - 1 ? "" : " ");
            }
            element->data.attributes["class"] = updated;
            mark_dom_dirty(element);
        }
    }

    return JS_UNDEFINED;
}

JSValue js_toggle_class_by_addr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;

    int64_t addr_int;
    if (JS_ToInt64Ext(ctx, &addr_int, argv[0])) return JS_UNDEFINED;

    const char* cls = JS_ToCString(ctx, argv[1]);
    if (!cls) return JS_UNDEFINED;
    std::string cls_str(cls);
    JS_FreeCString(ctx, cls);

    Node* node_ptr = reinterpret_cast<Node*>(addr_int);
    if (node_ptr && node_ptr->type() == NodeType::Element) {
        auto element = static_cast<ElementNode*>(node_ptr);
        auto classes = element->data.classes();

        bool has_force = argc >= 3 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2]);
        if (has_force) {
            bool force = JS_ToBool(ctx, argv[2]);
            auto it = std::find(classes.begin(), classes.end(), cls_str);
            bool currently_has = (it != classes.end());
            if (force && !currently_has) {
                classes.push_back(cls_str);
            } else if (!force && currently_has) {
                classes.erase(it);
            }
        } else {
            auto it = std::find(classes.begin(), classes.end(), cls_str);
            if (it != classes.end()) {
                classes.erase(it);
            } else {
                classes.push_back(cls_str);
            }
        }

        std::string updated;
        for (size_t i = 0; i < classes.size(); ++i) {
            updated += classes[i] + (i == classes.size() - 1 ? "" : " ");
        }
        element->data.attributes["class"] = updated;
        mark_dom_dirty(element);
    }

    return JS_UNDEFINED;
}

void install_dom_bindings(JSEngine& js)
{
    js.add_function("__getElementById", js_get_element_by_id);
    js.add_function("__querySelectorAll", js_query_selector_all);
    js.add_function("__querySelectorAllByAddr", js_query_selector_all_by_addr);
    js.add_function("__createElement", js_create_element);
    js.add_function("__appendChild", js_append_child);
    js.add_function("__insertBefore", js_insert_before);
    js.add_function("__removeNode", js_remove_node);
    js.add_function("__removeChild", js_remove_child);
    js.add_function("__getElementChildren", js_get_element_children);
    js.add_function("__getParentElement", js_get_parent_element);
    js.add_function("__getNextElementSibling", js_get_next_element_sibling);
    js.add_function("__getPreviousElementSibling", js_get_previous_element_sibling);
    js.add_function("__setTextContent", js_set_text_content);
    js.add_function("__getTextContent", js_get_text_content);
    js.add_function("__setInnerHTML", js_set_inner_html);
    js.add_function("__getInnerHTML", js_get_inner_html);
    js.add_function("__getClassName", js_get_class_name);
    js.add_function("__setClassName", js_set_class_name);
    js.add_function("__addClassByAddr", js_add_class_by_addr);
    js.add_function("__removeClassByAddr", js_remove_class_by_addr);
    js.add_function("__toggleClassByAddr", js_toggle_class_by_addr);
    js.add_function("__getAttribute", js_get_attribute);

    // `document`, and the wrapper of an element: the properties the wrapper has come from the functions in
    // document._wrappers, each added by the binding that owns them (styles, events, layout, form controls).
    js.execute(R"JS(
// copies the properties of `source` to `target` as they are defined, getters and setters included
const __mixin = (target, source) => {
  Reflect.ownKeys(source).forEach(key => Object.defineProperty(target, key, Object.getOwnPropertyDescriptor(source, key)));
};
const document = {
  _wrappers: [],
  _wrap: (node) => {
    const element = { _addr: node._addr, tagName: node.tagName };
    document._wrappers.forEach(part => __mixin(element, part(node)));
    return element;
  },
  createElement: (tagName) => {
    const node = __createElement(tagName);
    return document._wrap(node);
  },
  getElementById: (id) => {
    const node = __getElementById(id);
    return node ? document._wrap(node) : null;
  },
  getElementsByClassName: (name) => {
    return __querySelectorAll('.' + name).map(n => document._wrap(n));
  },
  querySelectorAll: (selector) => {
    return __querySelectorAll(selector).map(n => document._wrap(n));
  },
  querySelector: (selector) => {
    const all = document.querySelectorAll(selector);
    return all.length > 0 ? all[0] : null;
  },
  get body() { return document.querySelector('body'); },
  get documentElement() { return document.querySelector('html'); }
};
document._wrappers.push((node) => ({
  get id() { return __getAttribute(node._addr, 'id') || ''; },
  set id(val) { __setAttribute(node._addr, 'id', String(val)); },
  getAttribute: (name) => __getAttribute(node._addr, name),
  get className() { return __getClassName(node._addr); },
  set className(val) { __setClassName(node._addr, val); },
  get textContent() { return __getTextContent(node._addr); },
  set textContent(val) { __setTextContent(node._addr, val); },
  get innerText() { return __getTextContent(node._addr); },
  set innerText(val) { __setTextContent(node._addr, val, true); },
  get innerHTML() { return __getInnerHTML(node._addr); },
  set innerHTML(val) { __setInnerHTML(node._addr, val); },
  classList: {
    add: (...args) => __addClassByAddr(node._addr, ...args),
    remove: (...args) => __removeClassByAddr(node._addr, ...args),
    toggle: (cls, force) => __toggleClassByAddr(node._addr, cls, force),
    contains: (cls) => __getClassName(node._addr).split(/\s+/).indexOf(cls) >= 0
  },
  appendChild: (child) => {
    __appendChild(node._addr, child._addr);
    return child;
  },
  querySelectorAll: (selector) => {
    return __querySelectorAllByAddr(node._addr, selector).map(n => document._wrap(n));
  },
  querySelector: (selector) => {
    const all = __querySelectorAllByAddr(node._addr, selector).map(n => document._wrap(n));
    return all.length > 0 ? all[0] : null;
  }
}));
// The element tree. Each read resolves the current children, so a fresh array on every read is enough.
document._wrappers.push((node) => ({
  get children() { return __getElementChildren(node._addr).map(n => document._wrap(n)); },
  get childElementCount() { return __getElementChildren(node._addr).length; },
  get firstElementChild() { const c = this.children; return c.length > 0 ? c[0] : null; },
  get lastElementChild() { const c = this.children; return c.length > 0 ? c[c.length - 1] : null; },
  get parentNode() { const p = __getParentElement(node._addr); return p ? document._wrap(p) : null; },
  get parentElement() { const p = __getParentElement(node._addr); return p ? document._wrap(p) : null; },
  get nextElementSibling() { const s = __getNextElementSibling(node._addr); return s ? document._wrap(s) : null; },
  get previousElementSibling() { const s = __getPreviousElementSibling(node._addr); return s ? document._wrap(s) : null; },
  remove: () => { __removeNode(node._addr); },
  removeChild: (child) => {
    __removeChild(node._addr, child._addr);
    return child;
  },
  insertBefore: (newChild, refChild) => {
    if (refChild === null || refChild === undefined) __appendChild(node._addr, newChild._addr);
    else __insertBefore(node._addr, newChild._addr, refChild._addr);
    return newChild;
  }
}));
)JS");
}

} // namespace linweb
