#pragma once
#include "quickjs.h"

namespace linweb {

// Runs node-lite scripts in the background for scripts of the page (exposed as linweb.node).
JSValue js_node_available(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_node_start(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_node_poll(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_node_stop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

} // namespace linweb
