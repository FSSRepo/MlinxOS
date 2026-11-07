#pragma once
#include "quickjs.h"

namespace linweb {

class JSEngine;

// Registers print() and the `console` object (log, error, warn, info) the scripts see.
void install_console_bindings(JSEngine& js);

JSValue js_console_log(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_console_error(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_console_warn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

} // namespace linweb
