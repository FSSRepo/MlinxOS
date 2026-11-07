#pragma once
#include "quickjs.h"

namespace linweb {

// System power for scripts: linweb.system.powerOff() returns { ok, error }. On success the
// machine powers off and the call does not return.
JSValue js_power_off(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

} // namespace linweb
