#include "scripting/perf_bindings.h"
#include "app/module.h"

namespace linweb {

JSValue js_perf_toggle(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    (void)ctx;
    toggle_profiler();
    return JS_UNDEFINED;
}

} // namespace linweb
