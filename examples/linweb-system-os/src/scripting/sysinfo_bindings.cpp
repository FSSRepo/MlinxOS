#include "scripting/sysinfo_bindings.h"
#include "system/sysinfo.h"

namespace linweb {

namespace {

void set_str(JSContext* ctx, JSValue obj, const char* key, const std::string& value) {
    JS_SetPropertyStr(ctx, obj, key, JS_NewStringLen(ctx, value.data(), value.size()));
}

} // namespace

JSValue js_system_mounts(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    JSValue arr = JS_NewArray(ctx);
    uint32_t index = 0;
    for (const sysinfo::Mount& mount : sysinfo::mounts()) {
        JSValue obj = JS_NewObject(ctx);
        set_str(ctx, obj, "device", mount.device);
        set_str(ctx, obj, "mountpoint", mount.mountpoint);
        set_str(ctx, obj, "type", mount.type);
        JS_SetPropertyStr(ctx, obj, "readOnly", JS_NewBool(ctx, mount.read_only));
        JS_SetPropertyUint32(ctx, arr, index++, obj);
    }
    return arr;
}

JSValue js_system_memory(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    sysinfo::Memory memory = sysinfo::memory();
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "totalMB", JS_NewInt64(ctx, memory.total_kb / 1024));
    JS_SetPropertyStr(ctx, obj, "freeMB", JS_NewInt64(ctx, memory.free_kb / 1024));
    JS_SetPropertyStr(ctx, obj, "availableMB", JS_NewInt64(ctx, memory.available_kb / 1024));
    JS_SetPropertyStr(ctx, obj, "cachedMB", JS_NewInt64(ctx, memory.cached_kb / 1024));
    return obj;
}

} // namespace linweb
