#include "scripting/perf_bindings.h"
#include "system/perf.h"

namespace linweb {

JSValue js_perf_stats(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    perf::Stats stats = perf::stats();
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "scriptMs", JS_NewFloat64(ctx, stats.script_ms));
    JS_SetPropertyStr(ctx, obj, "styleMs", JS_NewFloat64(ctx, stats.style_ms));
    JS_SetPropertyStr(ctx, obj, "layoutMs", JS_NewFloat64(ctx, stats.layout_ms));
    JS_SetPropertyStr(ctx, obj, "paintMs", JS_NewFloat64(ctx, stats.paint_ms));
    JS_SetPropertyStr(ctx, obj, "swapMs", JS_NewFloat64(ctx, stats.swap_ms));
    JS_SetPropertyStr(ctx, obj, "frameMs", JS_NewFloat64(ctx, stats.frame_ms));
    JS_SetPropertyStr(ctx, obj, "fps", JS_NewFloat64(ctx, stats.fps));
    return obj;
}

JSValue js_perf_toggle(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    perf::toggle();
    return JS_UNDEFINED;
}

} // namespace linweb
