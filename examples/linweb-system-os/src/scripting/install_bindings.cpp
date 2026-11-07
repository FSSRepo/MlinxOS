#include "scripting/install_bindings.h"
#include "system/install.h"

namespace linweb {

namespace {

std::string arg_string(JSContext* ctx, int argc, JSValueConst* argv, int index) {
    if (index >= argc) return std::string();
    size_t len = 0;
    const char* text = JS_ToCStringLen(ctx, &len, argv[index]);
    std::string out = text ? std::string(text, len) : std::string();
    if (text) JS_FreeCString(ctx, text);
    return out;
}

void set_str(JSContext* ctx, JSValue obj, const char* key, const std::string& value) {
    JS_SetPropertyStr(ctx, obj, key, JS_NewStringLen(ctx, value.data(), value.size()));
}

} // namespace

JSValue js_install_start(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    install::Result result = install::start(arg_string(ctx, argc, argv, 0), arg_string(ctx, argc, argv, 1));
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "ok", JS_NewBool(ctx, result.ok));
    set_str(ctx, obj, "error", result.error);
    return obj;
}

JSValue js_install_status(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    install::Status status = install::status();
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "running", JS_NewBool(ctx, status.running));
    JS_SetPropertyStr(ctx, obj, "done", JS_NewBool(ctx, status.done));
    JS_SetPropertyStr(ctx, obj, "exitCode", JS_NewInt32(ctx, status.exit_code));
    set_str(ctx, obj, "log", status.log);
    return obj;
}

JSValue js_runs_from_disk(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewBool(ctx, install::runs_from_disk());
}

JSValue js_install_available(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewBool(ctx, install::available());
}

} // namespace linweb
