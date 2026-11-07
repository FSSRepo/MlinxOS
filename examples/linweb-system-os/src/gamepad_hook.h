#pragma once
#include "platform/keyboard_handler.h"

// Reads the gamepad and turns it into keys and events for the page (gamepad_input.cpp).
void poll_gamepad(linweb::KeyboardHandler& keyboard);
// True while the stick is pushed or a D-pad direction is held: the screen is drawn on every frame meanwhile.
bool gamepad_wants_frame();
