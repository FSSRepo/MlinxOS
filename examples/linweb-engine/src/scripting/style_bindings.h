#pragma once
#include "quickjs.h"

namespace linweb {

class JSEngine;

// Registers the style functions and gives every element its `style` object. Needs the document bindings.
void install_style_bindings(JSEngine& js);

JSValue js_set_style_property(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_set_style_property_by_id(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_set_style_property_by_addr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

} // namespace linweb
