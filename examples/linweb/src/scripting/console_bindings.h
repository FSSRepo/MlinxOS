#pragma once
#include "quickjs.h"

namespace linweb {

JSValue js_console_log(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_console_error(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_console_warn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

} // namespace linweb
