#include "scripting/window_bindings.h"
#include "renderer/renderer.h"

namespace linweb {

// The viewport the page is rendered into; window.innerWidth / window.innerHeight read it live.
JSValue js_viewport_width(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewInt32(ctx, Renderer::get_viewport_width());
}

JSValue js_viewport_height(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewInt32(ctx, Renderer::get_viewport_height());
}

void install_window_bindings(JSEngine& js)
{
    js.add_function("__viewportWidth", js_viewport_width);
    js.add_function("__viewportHeight", js_viewport_height);
    js.execute(R"JS(
// the engine's own properties live on __window; only what a page sets on `window` becomes a global
const __window = { _listeners: {} };
// a page's window is also the global scope: a property it sets (window.fn = ...) is a global name for the scripts
const window = new Proxy(__window, { set(target, key, value) { target[key] = value; globalThis[key] = value; return true; } });
// read from the renderer on every access, so a resized window reports the new size
Object.defineProperties(__window, {
  innerWidth: { get: () => __viewportWidth(), configurable: true },
  innerHeight: { get: () => __viewportHeight(), configurable: true }
});
)JS");
}

} // namespace linweb
