/* node-lite: QuickJS plus a small Node.js compatibility layer (require, process, fs, path, http, ...). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "quickjs.h"
#include "quickjs-libc.h"
#include "net_native.h"
#include "tls_native.h"

extern const unsigned char node_lite_js[];
extern const unsigned long node_lite_js_len;

/* Names such as "http" or "node:fs" are served by lib/node-lite.js, which builds the source of a
   small ES module that re-exports the builtin; everything else is a file handled by QuickJS. */
static JSModuleDef *module_loader(JSContext *ctx, const char *name, void *opaque, JSValueConst attributes)
{
    const char *id = strncmp(name, "node:", 5) == 0 ? name + 5 : name;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue source_fn = JS_GetPropertyStr(ctx, global, "__builtinModuleSource");
    JSModuleDef *module = NULL;
    int builtin = 0;

    if (JS_IsFunction(ctx, source_fn)) {
        JSValue arg = JS_NewString(ctx, id);
        JSValue source = JS_Call(ctx, source_fn, JS_UNDEFINED, 1, &arg);
        JS_FreeValue(ctx, arg);
        if (JS_IsString(source)) {
            size_t len;
            const char *text = JS_ToCStringLen(ctx, &len, source);
            JSValue func = JS_Eval(ctx, text, len, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
            JS_FreeCString(ctx, text);
            builtin = 1;
            if (!JS_IsException(func)) {
                module = JS_VALUE_GET_PTR(func);
                JS_FreeValue(ctx, func);
            }
        } else if (JS_IsException(source)) {
            builtin = 1;
        }
        JS_FreeValue(ctx, source);
    }
    JS_FreeValue(ctx, source_fn);
    JS_FreeValue(ctx, global);
    return builtin ? module : js_module_loader(ctx, name, opaque, attributes);
}

/* __evalNamed(source, filename): evaluates a script under a file name, so stack traces point at the user's files. */
static JSValue js_eval_named(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    size_t len;
    const char *source = JS_ToCStringLen(ctx, &len, argv[0]);
    const char *name = JS_ToCString(ctx, argv[1]);
    if (!source || !name) {
        JS_FreeCString(ctx, source);
        JS_FreeCString(ctx, name);
        return JS_EXCEPTION;
    }
    JSValue result = JS_Eval(ctx, source, len, name, JS_EVAL_TYPE_GLOBAL);
    JS_FreeCString(ctx, source);
    JS_FreeCString(ctx, name);
    return result;
}

static int eval_module(JSContext *ctx, const char *code, const char *name)
{
    JSValue val = JS_Eval(ctx, code, strlen(code), name, JS_EVAL_TYPE_MODULE);
    if (JS_IsException(val)) {
        js_std_dump_error(ctx);
        return -1;
    }
    JS_FreeValue(ctx, val);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s script.js|script.mjs [args...]\n", argv[0]);
        return 2;
    }

    JSRuntime *rt = JS_NewRuntime();
    if (!rt) { fprintf(stderr, "node-lite: cannot create the runtime\n"); return 1; }
    js_std_init_handlers(rt);
    JSContext *ctx = JS_NewContext(rt);
    if (!ctx) { fprintf(stderr, "node-lite: cannot create the context\n"); return 1; }

    JS_SetModuleLoaderFunc2(rt, NULL, module_loader, js_module_check_attributes, NULL);
    JS_SetHostPromiseRejectionTracker(rt, js_std_promise_rejection_tracker, NULL);
    js_std_add_helpers(ctx, argc, argv);

    /* QuickJS' own modules live under qjs:* so that "os" and "std" stay free for Node's. */
    js_init_module_std(ctx, "qjs:std");
    js_init_module_os(ctx, "qjs:os");
    int rc = 0;
    if (eval_module(ctx,
            "import * as std from 'qjs:std';\n"
            "import * as os from 'qjs:os';\n"
            "globalThis.__std = std; globalThis.__os = os;\n", "<bootstrap>")) {
        rc = 1;
    }
    node_lite_init_net(ctx);
    node_lite_init_tls(ctx);
    {
        JSValue global = JS_GetGlobalObject(ctx);
        JS_SetPropertyStr(ctx, global, "__evalNamed", JS_NewCFunction(ctx, js_eval_named, "__evalNamed", 2));
        JS_FreeValue(ctx, global);
    }

    if (!rc) {
        JSValue val = JS_Eval(ctx, (const char *)node_lite_js, node_lite_js_len, "node-lite.js", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(val)) { js_std_dump_error(ctx); rc = 1; }
        JS_FreeValue(ctx, val);
    }

    if (!rc) {
        JSValue global = JS_GetGlobalObject(ctx);
        JSValue run_main = JS_GetPropertyStr(ctx, global, "__runMain");
        JSValue arg = JS_NewString(ctx, argv[1]);
        JSValue val = JS_Call(ctx, run_main, JS_UNDEFINED, 1, &arg);
        if (JS_IsException(val)) { js_std_dump_error(ctx); rc = 1; }
        JS_FreeValue(ctx, val);
        JS_FreeValue(ctx, arg);
        JS_FreeValue(ctx, run_main);
        JS_FreeValue(ctx, global);
    }

    if (!rc) js_std_loop(ctx);

    js_std_free_handlers(rt);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return rc;
}
