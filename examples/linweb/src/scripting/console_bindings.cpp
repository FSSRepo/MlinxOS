#include "scripting/console_bindings.h"
#include <iostream>

namespace linweb {

JSValue js_console_log(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    for (int i = 0; i < argc; i++) {
        const char* str = JS_ToCString(ctx, argv[i]);
        if (str) {
            std::cout << str << (i == argc - 1 ? "" : " ");
            JS_FreeCString(ctx, str);
        }
    }
    std::cout << std::endl;
    return JS_UNDEFINED;
}

JSValue js_console_error(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    std::cerr << "ERROR:";
    if (argc > 0) std::cerr << " ";
    for (int i = 0; i < argc; i++) {
        const char* str = JS_ToCString(ctx, argv[i]);
        if (str) {
            std::cerr << str << (i == argc - 1 ? "" : " ");
            JS_FreeCString(ctx, str);
        }
    }
    std::cerr << std::endl;
    return JS_UNDEFINED;
}

JSValue js_console_warn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    std::cout << "WARN:";
    if (argc > 0) std::cout << " ";
    for (int i = 0; i < argc; i++) {
        const char* str = JS_ToCString(ctx, argv[i]);
        if (str) {
            std::cout << str << (i == argc - 1 ? "" : " ");
            JS_FreeCString(ctx, str);
        }
    }
    std::cout << std::endl;
    return JS_UNDEFINED;
}

} // namespace linweb
