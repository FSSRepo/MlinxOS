#include "scripting/style_bindings.h"
#include "scripting/js_engine.h"
#include "core/dom.h"
#include "style/style_engine.h"
#include "utils/dom_traversal.h"
#include <map>
#include <string>
#include <memory>
#include <algorithm>

namespace linweb {

extern std::shared_ptr<StyledNode> g_style_tree;
void mark_dom_dirty(const Node* node);

namespace {

// element.style exposes CSS names in camelCase (animationDelay, fontSize); the engine uses the kebab-case names
// (animation-delay, font-size). Names that are already kebab-case are left alone.
std::string camel_to_kebab(const std::string& name) {
    std::string result;
    result.reserve(name.size() + 4);
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c >= 'A' && c <= 'Z') {
            if (i > 0 && name[i - 1] != '-') result += '-';
            result += static_cast<char>(c - 'A' + 'a');
        } else if (c == '_') {
            result += '-';
        } else {
            result += c;
        }
    }
    return result;
}

// An inline style wins the cascade, so a trailing !important only has to be dropped from the stored value.
std::string clean_style_value(const std::string& value) {
    std::string cleaned = trim(value);
    static const std::string important = "!important";
    if (cleaned.size() >= important.size() &&
        cleaned.compare(cleaned.size() - important.size(), important.size(), important) == 0) {
        cleaned = trim(cleaned.substr(0, cleaned.size() - important.size()));
    }
    return cleaned;
}

ElementNode* element_from_addr(int64_t addr) {
    Node* node = reinterpret_cast<Node*>(addr);
    if (!node || node->type() != NodeType::Element) return nullptr;
    return static_cast<ElementNode*>(node);
}

// The overrides live on the DOM node, so they survive every rebuild of the style tree. An empty value removes
// the override instead of storing it, as the inline attribute would.
void store_override(ElementNode* element, const std::string& name, const std::string& value) {
    if (!element) return;
    const std::string property = camel_to_kebab(name);
    if (property.empty()) return;
    const std::string cleaned = clean_style_value(value);
    if (cleaned.empty()) element->style_overrides.erase(property);
    else element->style_overrides[property] = cleaned;
    mark_dom_dirty(element);   // the next frame rebuilds style and layout with the new inline value
}

std::string read_override(ElementNode* element, const std::string& name) {
    if (!element) return "";
    auto it = element->style_overrides.find(camel_to_kebab(name));
    return it != element->style_overrides.end() ? it->second : "";
}

} // namespace

JSValue js_set_style_property(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_UNDEFINED;

    const char* class_name = JS_ToCString(ctx, argv[0]);
    const char* property = JS_ToCString(ctx, argv[1]);
    const char* value = JS_ToCString(ctx, argv[2]);

    if (class_name && property && value && g_style_tree) {
        auto styled_node = find_by_class(g_style_tree, class_name);
        if (styled_node && styled_node->node->type() == NodeType::Element) {
            store_override(static_cast<ElementNode*>(styled_node->node.get()), property, value);
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
            store_override(static_cast<ElementNode*>(styled_node->node.get()), property, value);
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
    if (!property) return JS_UNDEFINED;
    std::string property_str(property);
    JS_FreeCString(ctx, property);

    // A null/undefined value becomes "null"/"undefined" or fails the conversion: both remove the override.
    std::string value_str;
    if (!JS_IsNull(argv[2]) && !JS_IsUndefined(argv[2])) {
        const char* value = JS_ToCString(ctx, argv[2]);
        if (value) {
            value_str = value;
            JS_FreeCString(ctx, value);
        }
    }
    if (value_str == "null" || value_str == "undefined") value_str.clear();

    store_override(element_from_addr(addr_int), property_str, value_str);
    return JS_UNDEFINED;
}

JSValue js_get_style_property_by_addr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_NewString(ctx, "");

    int64_t addr_int;
    if (JS_ToInt64Ext(ctx, &addr_int, argv[0])) return JS_NewString(ctx, "");
    const char* property = JS_ToCString(ctx, argv[1]);
    if (!property) return JS_NewString(ctx, "");
    std::string property_str(property);
    JS_FreeCString(ctx, property);

    const std::string value = read_override(element_from_addr(addr_int), property_str);
    return JS_NewString(ctx, value.c_str());
}

JSValue js_remove_style_property_by_addr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_NewString(ctx, "");

    int64_t addr_int;
    if (JS_ToInt64Ext(ctx, &addr_int, argv[0])) return JS_NewString(ctx, "");
    const char* property = JS_ToCString(ctx, argv[1]);
    if (!property) return JS_NewString(ctx, "");
    std::string property_str(property);
    JS_FreeCString(ctx, property);

    ElementNode* element = element_from_addr(addr_int);
    const std::string previous = read_override(element, property_str);
    if (element) {
        element->style_overrides.erase(camel_to_kebab(property_str));
        mark_dom_dirty(element);
    }
    return JS_NewString(ctx, previous.c_str());
}

JSValue js_get_style_css_text_by_addr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NewString(ctx, "");

    int64_t addr_int;
    if (JS_ToInt64Ext(ctx, &addr_int, argv[0])) return JS_NewString(ctx, "");

    ElementNode* element = element_from_addr(addr_int);
    if (!element || element->style_overrides.empty()) return JS_NewString(ctx, "");

    // Stable order, so the value of cssText does not depend on the hash order of the map.
    std::map<std::string, std::string> sorted(element->style_overrides.begin(), element->style_overrides.end());
    std::string text;
    for (const auto& [name, value] : sorted) {
        if (!text.empty()) text += " ";
        text += name + ": " + value + ";";
    }
    return JS_NewString(ctx, text.c_str());
}

JSValue js_set_style_css_text_by_addr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;

    int64_t addr_int;
    if (JS_ToInt64Ext(ctx, &addr_int, argv[0])) return JS_UNDEFINED;
    const char* text = JS_ToCString(ctx, argv[1]);
    if (!text) return JS_UNDEFINED;
    std::string css(text);
    JS_FreeCString(ctx, text);

    ElementNode* element = element_from_addr(addr_int);
    if (!element) return JS_UNDEFINED;

    // cssText replaces everything that was set before.
    element->style_overrides.clear();
    if (!css.empty()) {
        CSSTokenizer tokenizer(css);
        for (const auto& declaration : parse_declarations(tokenizer)) {
            if (declaration.name.empty()) continue;
            store_override(element, declaration.name, declaration.value);
        }
    }
    mark_dom_dirty(element);
    return JS_UNDEFINED;
}

void install_style_bindings(JSEngine& js)
{
    js.add_function("__setStyleProperty", js_set_style_property);
    js.add_function("__setStylePropertyById", js_set_style_property_by_id);
    js.add_function("__setStylePropertyByAddr", js_set_style_property_by_addr);
    js.add_function("__getStylePropertyByAddr", js_get_style_property_by_addr);
    js.add_function("__removeStylePropertyByAddr", js_remove_style_property_by_addr);
    js.add_function("__getStyleCssTextByAddr", js_get_style_css_text_by_addr);
    js.add_function("__setStyleCssTextByAddr", js_set_style_css_text_by_addr);
    // `style` is a Proxy over the inline overrides kept on the element: any CSS property in camelCase is stored
    // under its kebab-case name, reads return the last value assigned ('' when there is none) and the
    // CSSStyleDeclaration helpers are available. Every assignment marks the page dirty, so the next frame rebuilds.
    js.execute(R"JS(
document._wrappers.push((node) => {
  const __cssName = (name) => String(name).replace(/[A-Z]/g, (c) => '-' + c.toLowerCase());
  const __inline = () => new Proxy({}, {
    get(target, prop) {
      if (typeof prop !== 'string') return target[prop];
      if (prop === 'cssText') return __getStyleCssTextByAddr(node._addr);
      if (prop === 'setProperty') {
        return (name, value) => __setStylePropertyByAddr(node._addr, __cssName(name), value == null ? '' : String(value));
      }
      if (prop === 'getPropertyValue') return (name) => __getStylePropertyByAddr(node._addr, __cssName(name));
      if (prop === 'removeProperty') return (name) => __removeStylePropertyByAddr(node._addr, __cssName(name));
      return __getStylePropertyByAddr(node._addr, __cssName(prop));
    },
    set(target, prop, value) {
      if (typeof prop !== 'string') { target[prop] = value; return true; }
      if (prop === 'cssText') { __setStyleCssTextByAddr(node._addr, value == null ? '' : String(value)); return true; }
      __setStylePropertyByAddr(node._addr, __cssName(prop), value == null ? '' : String(value));
      return true;
    }
  });
  return { get style() { return __inline(); } };
});
)JS");
}

} // namespace linweb
