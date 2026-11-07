#pragma once

namespace linweb {

class KeyboardHandler;

namespace gamepad_input {

// Called once per frame. Reads the gamepad without blocking: the D-pad or left stick send the arrow keys,
// cross sends Enter, triangle sends Escape and R2 toggles the profiler. The keys go through the keyboard
// path, so panels that listen to keydown work with the gamepad too. Nothing runs in the scripts per frame, except the
// analog stick events below while the stick is pushed.
// When a gamepad is opened or lost the page gets window "gamepadconnected" / "gamepaddisconnected" events whose
// event.gamepad is { id: <device name>, index: 0, connected: <bool> }.
// The arrow keys a pad makes carry event.source: "hat" for the D-pad (held, it repeats) and "stick" for the left stick.
// While the stick is pushed, window gets "gamepadaxis" events on every frame with { x, y } in -1..1 (dead zone removed),
// and one with 0, 0 when it returns to the centre: for panning, where a key press per push would be too coarse.
void poll(KeyboardHandler& keyboard);

// True while the stick is pushed or a D-pad direction is held: the screen is drawn on every frame meanwhile.
bool active();

} // namespace gamepad_input
} // namespace linweb
