#pragma once
#include "quickjs.h"

namespace linweb {

// Input devices for scripts: linweb.system.inputDevices() returns [{ name, handlers, axes }].
JSValue js_input_devices(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

} // namespace linweb
