#include "scripting/nodelite_bindings.h"
#include "system/nodelite.h"
#include <string>
#include <vector>

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

int arg_int(JSContext* ctx, int argc, JSValueConst* argv, int index, int fallback) {
    int32_t value = fallback;
    if (index < argc && JS_ToInt32(ctx, &value, argv[index]) != 0) return fallback;
    return value;
}

void set_str(JSContext* ctx, JSValue obj, const char* key, const std::string& value) {
    JS_SetPropertyStr(ctx, obj, key, JS_NewStringLen(ctx, value.data(), value.size()));
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> out;
    if (text.empty()) return out;
    size_t start = 0;
    for (;;) {
        size_t cut = text.find('\n', start);
        out.push_back(text.substr(start, cut == std::string::npos ? std::string::npos : cut - start));
        if (cut == std::string::npos) break;
        start = cut + 1;
    }
    return out;
}

} // namespace

JSValue js_node_available(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewBool(ctx, nodelite::available());
}

// __nodeStart(mode, text, args, timeoutMs): mode "file" runs the script `text`, mode "eval" runs the code
// `text`; args are the script arguments separated by newlines.
JSValue js_node_start(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    std::string mode = arg_string(ctx, argc, argv, 0);
    std::string text = arg_string(ctx, argc, argv, 1);
    std::vector<std::string> args = split_lines(arg_string(ctx, argc, argv, 2));
    int timeout = arg_int(ctx, argc, argv, 3, 60000);

    nodelite::StartResult result;
    if (mode == "file") result = nodelite::start_file(text, args, timeout);
    else if (mode == "eval") result = nodelite::start_eval(text, timeout);
    else result.error = "mode must be \"file\" or \"eval\"";

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "ok", JS_NewBool(ctx, result.ok));
    JS_SetPropertyStr(ctx, obj, "id", JS_NewInt32(ctx, result.id));
    set_str(ctx, obj, "error", result.error);
    return obj;
}

JSValue js_node_poll(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    nodelite::PollResult result = nodelite::poll(arg_int(ctx, argc, argv, 0, 0));
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "found", JS_NewBool(ctx, result.found));
    JS_SetPropertyStr(ctx, obj, "running", JS_NewBool(ctx, result.running));
    JS_SetPropertyStr(ctx, obj, "timedOut", JS_NewBool(ctx, result.timed_out));
    JS_SetPropertyStr(ctx, obj, "exitCode", JS_NewInt32(ctx, result.exit_code));
    set_str(ctx, obj, "output", result.output);
    return obj;
}

JSValue js_node_stop(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    nodelite::stop(arg_int(ctx, argc, argv, 0, 0));
    return JS_UNDEFINED;
}

} // namespace linweb
