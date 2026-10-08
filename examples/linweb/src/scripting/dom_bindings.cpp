#include "scripting/dom_bindings.h"
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
extern bool g_dom_dirty;
extern class JSEngine* g_js_engine;

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
    auto root_shared = find_shared_ptr_by_addr(root_ptr);
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

JSValue js_append_child(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;

    int64_t parent_addr, child_addr;
    if (JS_ToInt64Ext(ctx, &parent_addr, argv[0])) return JS_UNDEFINED;
    if (JS_ToInt64Ext(ctx, &child_addr, argv[1])) return JS_UNDEFINED;

    Node* parent_ptr = reinterpret_cast<Node*>(parent_addr);
    Node* child_ptr = reinterpret_cast<Node*>(child_addr);

    if (parent_ptr && child_ptr) {
        std::shared_ptr<Node> child_shared;

        auto it = std::find_if(g_detached_nodes.begin(), g_detached_nodes.end(),
            [child_ptr](const std::shared_ptr<Node>& n) { return n.get() == child_ptr; });

        if (it != g_detached_nodes.end()) {
            child_shared = *it;
            g_detached_nodes.erase(it);
        } else {
            std::function<std::shared_ptr<Node>(std::shared_ptr<Node>)> find_shared;
            find_shared = [&](std::shared_ptr<Node> n) -> std::shared_ptr<Node> {
                if (n.get() == child_ptr) return n;
                for (auto& c : n->children) {
                    auto found = find_shared(c);
                    if (found) return found;
                }
                return nullptr;
            };

            if (g_dom_root) child_shared = find_shared(g_dom_root);
        }

        if (child_shared) {
            if (auto old_parent = child_shared->parent.lock()) {
                auto& children = old_parent->children;
                children.erase(std::remove(children.begin(), children.end(), child_shared), children.end());
            }

            std::shared_ptr<Node> parent_shared;
            std::function<std::shared_ptr<Node>(std::shared_ptr<Node>)> find_parent_shared;
            find_parent_shared = [&](std::shared_ptr<Node> n) -> std::shared_ptr<Node> {
                if (n.get() == parent_ptr) return n;
                for (auto& c : n->children) {
                    auto found = find_parent_shared(c);
                    if (found) return found;
                }
                return nullptr;
            };

            if (g_dom_root) parent_shared = find_parent_shared(g_dom_root);

            if (parent_shared) {
                parent_shared->children.push_back(child_shared);
                child_shared->parent = parent_shared;
                g_dom_dirty = true;
            } else {
                auto it_p = std::find_if(g_detached_nodes.begin(), g_detached_nodes.end(),
                    [parent_ptr](const std::shared_ptr<Node>& n) { return n.get() == parent_ptr; });
                if (it_p != g_detached_nodes.end()) {
                    parent_shared = *it_p;
                    parent_shared->children.push_back(child_shared);
                    child_shared->parent = parent_shared;
                }
            }
        }
    }

    return JS_UNDEFINED;
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
        auto text_node = std::make_shared<TextNode>(text);

        auto parent_shared = find_shared_ptr_by_addr(node_ptr);
        if (parent_shared) {
            node_ptr->children.push_back(text_node);
            text_node->parent = parent_shared;
        } else {
            node_ptr->children.push_back(text_node);
        }
        g_dom_dirty = true;
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
        auto shared = find_shared_ptr_by_addr(node_ptr);
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
                auto parent_shared = find_shared_ptr_by_addr(node_ptr);
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
        g_dom_dirty = true;
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
        auto shared = find_shared_ptr_by_addr(node_ptr);
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
        g_dom_dirty = true;
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
            g_dom_dirty = true;
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
            g_dom_dirty = true;
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
        g_dom_dirty = true;
    }

    return JS_UNDEFINED;
}

} // namespace linweb
