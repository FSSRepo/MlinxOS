#pragma once
#include <memory>
#include "quickjs.h"
#include "core/dom.h"

namespace linweb {

class JSEngine;

// The shared node behind an address a wrapper holds, including nodes of a subtree that is not attached to the document.
std::shared_ptr<Node> find_dom_node_by_address(Node* ptr);

// Registers the DOM functions and defines `document` and the element wrapper the other bindings add properties to
// (document._wrappers). Install it before the bindings that extend elements.
void install_dom_bindings(JSEngine& js);

JSValue js_get_element_by_id(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_query_selector_all(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_query_selector_all_by_addr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_create_element(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_append_child(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_insert_before(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_remove_node(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_remove_child(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_get_element_children(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_get_parent_element(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_get_next_element_sibling(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_get_previous_element_sibling(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_set_text_content(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_get_text_content(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_set_inner_html(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_get_inner_html(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_get_class_name(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_set_class_name(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_get_attribute(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_add_class_by_addr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_remove_class_by_addr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_toggle_class_by_addr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

} // namespace linweb
