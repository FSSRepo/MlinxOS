#pragma once
#include "quickjs.h"

namespace linweb {

JSValue js_set_style_property(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_set_style_property_by_id(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_set_style_property_by_addr(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

} // namespace linweb
