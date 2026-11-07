#pragma once
#include "quickjs.h"

namespace linweb {

// linweb.perf.toggle(): shows or hides the profiler overlay of the engine (R2 on the gamepad; F3 is handled in the keyboard).
JSValue js_perf_toggle(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

} // namespace linweb
