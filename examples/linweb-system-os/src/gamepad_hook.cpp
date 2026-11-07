#include "gamepad_hook.h"
#include "gamepad_input.h"

// The gamepad code of the module (gamepad_input.cpp): the keys it makes and the events it sends go to the page.
void poll_gamepad(linweb::KeyboardHandler& keyboard) {
    linweb::gamepad_input::poll(keyboard);
}

// The stick and the D-pad move something on the page on every frame while they are held.
bool gamepad_wants_frame() {
    return linweb::gamepad_input::active();
}
