#include "forms/form_bindings.h"
#include "forms/form_controls.h"
#include "scripting/js_engine.h"
#include "utils/dom_traversal.h"
#include "utils/log.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace linweb {

void mark_dom_dirty(const Node* node);

namespace forms {

namespace {

JSEngine* g_js = nullptr;
bool g_prevented = false;

// JavaScript string literal (quotes, backslashes and control characters escaped).
std::string js_quote(const std::string& text) {
    std::string out = "'";
    for (unsigned char c : text) {
        if (c == '\\' || c == '\'') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 0x20 || c == 0x7f) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\x%02x", c); out += buf; }
        else out += static_cast<char>(c);
    }
    return out + "'";
}

std::shared_ptr<Node> node_from(JSContext* ctx, JSValueConst value) {
    int64_t addr = 0;
    if (JS_ToInt64Ext(ctx, &addr, value)) return nullptr;
    return find_shared_ptr_by_addr(reinterpret_cast<Node*>(addr));
}

std::string string_arg(JSContext* ctx, int argc, JSValueConst* argv, int index) {
    if (index >= argc) return std::string();
    const char* text = JS_ToCString(ctx, argv[index]);
    std::string out = text ? text : "";
    if (text) JS_FreeCString(ctx, text);
    return out;
}

void emit_to_js(const std::shared_ptr<Node>& node, const std::string& type) {
    if (!g_js || !node || node->type() != NodeType::Element) return;
    auto element = std::static_pointer_cast<ElementNode>(node);
    log_event("form", type + " " + node->tag_name + (element->data.id().empty() ? "" : "#" + element->data.id()) +
                      " value=" + value_of(*node));
    std::string value = js_quote(value_of(*node));
    std::string data = "{ type: " + js_quote(type) + ", value: " + value +
                       ", target: { value: " + value + ", id: " + js_quote(element->data.id()) + " } }";

    // the same targets the mouse events use: the element's address, its id and its classes
    g_js->dispatch_event(std::to_string(reinterpret_cast<int64_t>(node.get())), type, data);
    if (!element->data.id().empty()) g_js->dispatch_event(element->data.id(), type, data);
    for (const std::string& cls : element->data.classes()) g_js->dispatch_event(cls, type, data);

    // inline handlers: <input oninput="..."> and onchange
    auto handler = element->data.attributes.find("on" + type);
    if (handler != element->data.attributes.end() && !handler->second.empty()) g_js->execute(handler->second);
}

} // namespace

void install_events(JSEngine* js) {
    g_js = js;
    set_emitter(emit_to_js);
}

bool take_default_prevented() {
    bool was = g_prevented;
    g_prevented = false;
    return was;
}

JSValue js_get_value(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NewString(ctx, "");
    auto node = node_from(ctx, argv[0]);
    if (!node) return JS_NewString(ctx, "");
    return JS_NewString(ctx, value_of(*node).c_str());
}

JSValue js_set_value(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;
    auto node = node_from(ctx, argv[0]);
    if (node) set_value(node, string_arg(ctx, argc, argv, 1));
    return JS_UNDEFINED;
}

JSValue js_set_attribute(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_UNDEFINED;
    auto node = node_from(ctx, argv[0]);
    if (!node || node->type() != NodeType::Element) return JS_UNDEFINED;
    std::string name = string_arg(ctx, argc, argv, 1);
    if (name == "value" && kind_of(*node) != Kind::None) set_value(node, string_arg(ctx, argc, argv, 2));
    else node->data.attributes[name] = string_arg(ctx, argc, argv, 2);
    mark_dom_dirty(node.get());   // the attribute may be used by styles or change the size of a control
    return JS_UNDEFINED;
}

JSValue js_remove_attribute(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;
    auto node = node_from(ctx, argv[0]);
    if (node && node->type() == NodeType::Element) {
        node->data.attributes.erase(string_arg(ctx, argc, argv, 1));
        mark_dom_dirty(node.get());
    }
    return JS_UNDEFINED;
}

JSValue js_has_attribute(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_FALSE;
    auto node = node_from(ctx, argv[0]);
    if (!node || node->type() != NodeType::Element) return JS_FALSE;
    return JS_NewBool(ctx, node->data.attributes.count(string_arg(ctx, argc, argv, 1)) > 0);
}

JSValue js_focus_node(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_UNDEFINED;
    auto node = node_from(ctx, argv[0]);
    if (node && node->type() == NodeType::Element) set_focus(node);
    return JS_UNDEFINED;
}

JSValue js_open_picker(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_UNDEFINED;
    auto node = node_from(ctx, argv[0]);
    if (node && node->type() == NodeType::Element) open_picker(node);
    return JS_UNDEFINED;
}

JSValue js_blur_node(JSContext*, JSValueConst, int, JSValueConst*) {
    blur();
    return JS_UNDEFINED;
}

JSValue js_get_active_element(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    std::shared_ptr<Node> node = focused();
    if (!node || node->type() != NodeType::Element) return JS_NULL;
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "_addr", JS_NewBigInt64(ctx, reinterpret_cast<int64_t>(node.get())));
    JS_SetPropertyStr(ctx, obj, "tagName", JS_NewString(ctx, node->tag_name.c_str()));
    return obj;
}

JSValue js_prevent_default(JSContext*, JSValueConst, int, JSValueConst*) {
    g_prevented = true;
    return JS_UNDEFINED;
}

void install_bindings(JSEngine& js) {
    js.add_function("__getValue", js_get_value);
    js.add_function("__setValue", js_set_value);
    js.add_function("__setAttribute", js_set_attribute);
    js.add_function("__removeAttribute", js_remove_attribute);
    js.add_function("__hasAttribute", js_has_attribute);
    js.add_function("__focusNode", js_focus_node);
    js.add_function("__openPicker", js_open_picker);
    js.add_function("__blurNode", js_blur_node);
    js.add_function("__getActiveElement", js_get_active_element);
    js.add_function("__preventDefault", js_prevent_default);

    js.execute(R"JS(
__mixin(document, {
  get activeElement() {
    const node = __getActiveElement();
    return node ? document._wrap(node) : null;
  }
});
document._wrappers.push((node) => ({
  setAttribute: (name, val) => __setAttribute(node._addr, name, String(val)),
  removeAttribute: (name) => __removeAttribute(node._addr, name),
  hasAttribute: (name) => __hasAttribute(node._addr, name),
  get value() { return __getValue(node._addr); },
  set value(val) { __setValue(node._addr, String(val)); },
  get valueAsNumber() { return parseFloat(__getValue(node._addr)); },
  get disabled() { return __hasAttribute(node._addr, 'disabled'); },
  set disabled(val) { if (val) __setAttribute(node._addr, 'disabled', ''); else __removeAttribute(node._addr, 'disabled'); },
  get min() { return __getAttribute(node._addr, 'min') || ''; },
  set min(val) { __setAttribute(node._addr, 'min', String(val)); },
  get max() { return __getAttribute(node._addr, 'max') || ''; },
  set max(val) { __setAttribute(node._addr, 'max', String(val)); },
  get step() { return __getAttribute(node._addr, 'step') || ''; },
  set step(val) { __setAttribute(node._addr, 'step', String(val)); },
  get placeholder() { return __getAttribute(node._addr, 'placeholder') || ''; },
  set placeholder(val) { __setAttribute(node._addr, 'placeholder', String(val)); },
  focus: () => __focusNode(node._addr),
  showPicker: () => __openPicker(node._addr),
  blur: () => __blurNode()
}));
)JS");

    install_events(&js);
}

} // namespace forms
} // namespace linweb
