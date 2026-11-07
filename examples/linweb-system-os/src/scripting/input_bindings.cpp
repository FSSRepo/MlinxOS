#include "scripting/input_bindings.h"
#include "system/input.h"

namespace linweb {

namespace {

void set_str(JSContext* ctx, JSValue obj, const char* key, const std::string& value) {
    JS_SetPropertyStr(ctx, obj, key, JS_NewStringLen(ctx, value.data(), value.size()));
}

} // namespace

JSValue js_input_devices(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    JSValue arr = JS_NewArray(ctx);
    uint32_t index = 0;
    for (const input::Device& device : input::list_devices()) {
        JSValue obj = JS_NewObject(ctx);
        set_str(ctx, obj, "name", device.name);
        set_str(ctx, obj, "handlers", device.handlers);
        JS_SetPropertyStr(ctx, obj, "axes", JS_NewBool(ctx, device.axes));
        JS_SetPropertyUint32(ctx, arr, index++, obj);
    }
    return arr;
}

} // namespace linweb
