#pragma once
#include "quickjs.h"

namespace linweb {

// The OpenGL implementation behind the window and its pending errors, for scripts (linweb.gl, linweb.renderer and
// linweb.debug). They call glGetString and glGetError, so the GL context must be current when a page calls them.
JSValue js_gl_renderer_info(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_gl_drain_errors(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

} // namespace linweb
