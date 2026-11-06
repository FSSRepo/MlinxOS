#include "scripting/power_bindings.h"
#include "system/power.h"

namespace linweb {

JSValue js_power_off(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    power::Result result = power::power_off();
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "ok", JS_NewBool(ctx, result.ok));
    JS_SetPropertyStr(ctx, obj, "error", JS_NewStringLen(ctx, result.error.data(), result.error.size()));
    return obj;
}

} // namespace linweb
