#include "scripting/js_context.h"
#include "utils/log.h"
#include <iostream>

namespace linweb {

JSContextWrapper::JSContextWrapper() : ctx(nullptr) {}

JSContextWrapper::~JSContextWrapper() {
    destroy();
}

void JSContextWrapper::create(JSRuntimeWrapper& runtime) {
    if (!ctx && runtime.get()) {
        ctx = JS_NewContext(runtime.get());
    }
}

void JSContextWrapper::destroy() {
    if (ctx) {
        JS_FreeContext(ctx);
        ctx = nullptr;
    }
}

bool JSContextWrapper::evaluate(const std::string& code, const std::string& filename) {
    if (!ctx) return false;

    JSValue val = JS_Eval(ctx, code.c_str(), code.length(), filename.c_str(), JS_EVAL_TYPE_GLOBAL);
    bool success = !JS_IsException(val);

    if (!success) {
        JSValue exception = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, exception);
        std::cerr << "JS Error: " << msg << std::endl;
        log_event("js-error", std::string(msg) + " (" + filename + ")");
        JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, exception);
    }

    JS_FreeValue(ctx, val);
    run_pending_jobs();
    return success;
}

void JSContextWrapper::run_pending_jobs() {
    if (!ctx) return;
    JSRuntime* rt = JS_GetRuntime(ctx);
    JSContext* job_ctx = nullptr;
    for (;;) {
        int status = JS_ExecutePendingJob(rt, &job_ctx);
        if (status > 0) continue;
        if (status < 0) {
            JSValue exception = JS_GetException(job_ctx ? job_ctx : ctx);
            const char* msg = JS_ToCString(ctx, exception);
            std::cerr << "JS Error (promise): " << (msg ? msg : "?") << std::endl;
            if (msg) JS_FreeCString(ctx, msg);
            JS_FreeValue(ctx, exception);
            continue;
        }
        break;
    }
}

void JSContextWrapper::register_function(const std::string& name, JSCFunction* func) {
    if (!ctx) return;

    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, name.c_str(), JS_NewCFunction(ctx, func, name.c_str(), 1));
    JS_FreeValue(ctx, global_obj);
}

bool JSContextWrapper::call_function(const std::string& name) {
    if (!ctx) return false;

    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue func = JS_GetPropertyStr(ctx, global_obj, name.c_str());
    bool success = true;

    if (JS_IsFunction(ctx, func)) {
        JSValue result = JS_Call(ctx, func, global_obj, 0, nullptr);
        if (JS_IsException(result)) {
            JSValue exception = JS_GetException(ctx);
            const char* msg = JS_ToCString(ctx, exception);
            std::cerr << "JS Error in " << name << ": " << msg << std::endl;
            JS_FreeCString(ctx, msg);
            JS_FreeValue(ctx, exception);
            success = false;
        }
        JS_FreeValue(ctx, result);
    }

    JS_FreeValue(ctx, func);
    JS_FreeValue(ctx, global_obj);
    run_pending_jobs();
    return success;
}

} // namespace linweb
