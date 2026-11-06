#pragma once
#include <memory>
#include <string>
#include "core/dom.h"
#include "scripting/js_engine.h"

namespace linweb {

// Gives the scripts of the page their browser-like API: the native functions, `window`, `document`, `console` and the
// timers, and the form controls' events.
void install_js_api(JSEngine& js, const std::shared_ptr<Node>& dom);

// Runs the page's <script> elements in document order (external files resolved against `base_dir`), then fires
// DOMContentLoaded.
void run_page_scripts(JSEngine& js, const std::shared_ptr<Node>& dom, const std::string& base_dir);

} // namespace linweb
