#include "scripting/console_bindings.h"
#include "scripting/js_engine.h"
#include "gl_wrapper.h"
#include "platform/platform_config.h"
#include <cctype>
#include <cstdio>
#include <iostream>
#include <string>

namespace linweb {

static JSValue gl_string(JSContext* ctx, GLenum name) {
    const GLubyte* value = glGetString(name);
    return JS_NewString(ctx, value ? reinterpret_cast<const char*>(value) : "unavailable");
}

JSValue js_gl_renderer_info(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    JSValue info = JS_NewObject(ctx);
#if defined(LINWEB_PLATFORM_WESTON)
    const char* session = "Wayland / Weston / EGL";
#elif defined(LINWEB_PLATFORM_XORG)
    const char* session = "X.Org / EGL / OpenGL";
#else
    const char* session = "GLFW / OpenGL";
#endif
    JS_SetPropertyStr(ctx, info, "session", JS_NewString(ctx, session));
    JS_SetPropertyStr(ctx, info, "vendor", gl_string(ctx, GL_VENDOR));
    JS_SetPropertyStr(ctx, info, "renderer", gl_string(ctx, GL_RENDERER));
    JS_SetPropertyStr(ctx, info, "version", gl_string(ctx, GL_VERSION));
    JS_SetPropertyStr(ctx, info, "shadingLanguageVersion", gl_string(ctx, GL_SHADING_LANGUAGE_VERSION));
    const GLubyte* renderer = glGetString(GL_RENDERER);
    std::string name = renderer ? reinterpret_cast<const char*>(renderer) : "";
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    bool software = name.find("llvmpipe") != std::string::npos || name.find("softpipe") != std::string::npos || name.find("swrast") != std::string::npos;
    JS_SetPropertyStr(ctx, info, "software", JS_NewBool(ctx, software));
    return info;
}

JSValue js_gl_drain_errors(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    JSValue errors = JS_NewArray(ctx);
    for (uint32_t i = 0; i < 32; ++i) {
        GLenum error = glGetError();
        if (error == GL_NO_ERROR) break;
        char value[16];
        std::snprintf(value, sizeof(value), "0x%04X", static_cast<unsigned int>(error));
        JS_SetPropertyUint32(ctx, errors, i, JS_NewString(ctx, value));
    }
    return errors;
}

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

void install_console_bindings(JSEngine& js)
{
    js.add_function("print", js_console_log);
    js.execute(R"JS(
const console = {
  log: (...args) => print(...args),
  error: (...args) => print('ERROR:', ...args),
  warn: (...args) => print('WARN:', ...args),
  info: (...args) => print('INFO:', ...args)
};
)JS");
}

} // namespace linweb
