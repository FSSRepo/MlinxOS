#pragma once
#include "quickjs.h"

namespace linweb {

// Frame timings for scripts: linweb.perf.stats() returns { scriptMs, styleMs, layoutMs, paintMs, swapMs, frameMs, fps }.
JSValue js_perf_stats(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
// linweb.perf.toggle(): shows or hides the native overlay (R2 on the gamepad; F3 is handled in the keyboard).
JSValue js_perf_toggle(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

} // namespace linweb
