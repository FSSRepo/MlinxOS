#include "scripting/gl_info.h"
#include "gl_wrapper.h"
#include "platform/platform_config.h"
#include <cctype>
#include <cstdio>
#include <string>

namespace linweb {

namespace {

JSValue gl_string(JSContext* ctx, GLenum name) {
    const GLubyte* value = glGetString(name);
    return JS_NewString(ctx, value ? reinterpret_cast<const char*>(value) : "unavailable");
}

} // namespace

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

} // namespace linweb
