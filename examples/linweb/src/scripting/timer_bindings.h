#pragma once

namespace linweb {

// Timer bindings are currently implemented in JavaScript via the window.setTimeout /
// window.setInterval API. These C++ stubs exist for future native timer integration.
void register_timer_bindings();

} // namespace linweb
