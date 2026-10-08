#pragma once
#include "quickjs.h"

namespace linweb {

JSValue js_get_element_by_id(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_query_selector_all(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_query_selector_all_by_addr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_create_element(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_append_child(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
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
