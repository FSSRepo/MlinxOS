#pragma once

namespace linweb {

class KeyboardHandler;

namespace gamepad_input {

// Called once per frame. Reads the gamepad without blocking: the D-pad or left stick send the arrow keys,
// cross sends Enter, triangle sends Escape and R2 toggles the profiler. The keys go through the keyboard
// path, so panels that listen to keydown work with the gamepad too. Nothing runs in the scripts per frame.
void poll(KeyboardHandler& keyboard);

} // namespace gamepad_input
} // namespace linweb
