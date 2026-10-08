#include "scripting/style_bindings.h"
#include "core/dom.h"
#include "style/style_engine.h"
#include "utils/dom_traversal.h"
#include <string>
#include <memory>
#include <algorithm>

namespace linweb {

extern std::shared_ptr<StyledNode> g_style_tree;

JSValue js_set_style_property(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_UNDEFINED;

    const char* class_name = JS_ToCString(ctx, argv[0]);
    const char* property = JS_ToCString(ctx, argv[1]);
    const char* value = JS_ToCString(ctx, argv[2]);

    if (class_name && property && value && g_style_tree) {
        auto styled_node = find_by_class(g_style_tree, class_name);
        if (styled_node && styled_node->node->type() == NodeType::Element) {
            auto element = std::static_pointer_cast<ElementNode>(styled_node->node);
            element->style_overrides[property] = value;
        }
    }

    if (class_name) JS_FreeCString(ctx, class_name);
    if (property) JS_FreeCString(ctx, property);
    if (value) JS_FreeCString(ctx, value);

    return JS_UNDEFINED;
}

JSValue js_set_style_property_by_id(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_UNDEFINED;

    const char* id = JS_ToCString(ctx, argv[0]);
    const char* property = JS_ToCString(ctx, argv[1]);
    const char* value = JS_ToCString(ctx, argv[2]);

    if (id && property && value && g_style_tree) {
        auto styled_node = find_by_id(g_style_tree, id);
        if (styled_node && styled_node->node->type() == NodeType::Element) {
            auto element = std::static_pointer_cast<ElementNode>(styled_node->node);
            element->style_overrides[property] = value;
        }
    }

    if (id) JS_FreeCString(ctx, id);
    if (property) JS_FreeCString(ctx, property);
    if (value) JS_FreeCString(ctx, value);

    return JS_UNDEFINED;
}

JSValue js_set_style_property_by_addr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_UNDEFINED;

    int64_t addr_int;
    if (JS_ToInt64Ext(ctx, &addr_int, argv[0])) return JS_UNDEFINED;
    const char* property = JS_ToCString(ctx, argv[1]);
    const char* value = JS_ToCString(ctx, argv[2]);

    if (property && value) {
        Node* node_ptr = reinterpret_cast<Node*>(addr_int);
        if (node_ptr && node_ptr->type() == NodeType::Element) {
            auto element = static_cast<ElementNode*>(node_ptr);
            element->style_overrides[property] = value;
        }
    }

    if (property) JS_FreeCString(ctx, property);
    if (value) JS_FreeCString(ctx, value);

    return JS_UNDEFINED;
}

} // namespace linweb
