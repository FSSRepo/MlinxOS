#pragma once
#include "scripting/js_engine.h"

namespace linweb {

// Creates the `window` object the other bindings add to (events, timers). A property a script sets on it is also a
// global name for the scripts, as in a browser. It also adds innerWidth/innerHeight from the renderer's viewport.
// Install it before them.
void install_window_bindings(JSEngine& js);

} // namespace linweb
