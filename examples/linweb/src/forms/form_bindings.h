#pragma once
#include "quickjs.h"

namespace linweb {

class JSEngine;

namespace forms {

// Connects the form controls to JavaScript: their "input", "change", "focus" and "blur" events reach the listeners of
// the element (by address, id and class, like the mouse events) with { type, value, target: { value, id } }.
void install_events(JSEngine* js);

// Registers the native functions behind the form controls, gives every element value, focus(), blur(), setAttribute() and
// the like, and document.activeElement, and connects the control events (install_events). Needs the document bindings.
void install_bindings(JSEngine& js);

// True once when a script called preventDefault() since the last call, so the engine can skip the default action
// (typing into a control, Tab).
bool take_default_prevented();

// Native functions behind element.value, focus(), blur(), setAttribute() and document.activeElement.
JSValue js_get_value(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_set_value(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_set_attribute(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_remove_attribute(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_has_attribute(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_focus_node(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_blur_node(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_open_picker(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_get_active_element(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_prevent_default(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

} // namespace forms
} // namespace linweb
