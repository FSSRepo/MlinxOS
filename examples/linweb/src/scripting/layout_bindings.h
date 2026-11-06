#pragma once
#include "quickjs.h"

namespace linweb { class JSEngine; }

// Registers the layout/geometry functions and gives every element getBoundingClientRect(), offset*/client* and scrollTop.
// Needs the document bindings.
void install_layout_bindings(linweb::JSEngine& js);

JSValue js_flush_layout(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_get_bounding_rect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_get_element_metrics(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_get_scroll_top(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_set_scroll_top(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
