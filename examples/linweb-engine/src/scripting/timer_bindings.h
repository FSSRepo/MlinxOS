#pragma once
#include "scripting/js_engine.h"

namespace linweb {

// setTimeout/clearTimeout, setInterval/clearInterval, requestAnimationFrame/cancelAnimationFrame (globals and on
// window), and the _run_raf the main loop calls every frame to run them. Kept in JavaScript, driven by the frame loop.
// Needs the window bindings.
void install_timer_bindings(JSEngine& js);

} // namespace linweb
