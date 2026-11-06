#pragma once

namespace linweb {

class JSEngine;

// Registers the functions behind the system features (GL info, storage, audio, video, wifi, network, web server, Node,
// power, input devices, gamepad, install, memory) and defines the `linweb` object the scripts call them through, also
// reachable as window.linweb. Needs the window bindings.
void install_native_bindings(JSEngine& js);

} // namespace linweb
