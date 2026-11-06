#pragma once
#include "quickjs.h"
#include <string>

namespace linweb {

// Gamepad for scripts: linweb.gamepad.open() returns the name of the gamepad it opened ('' when
// none), and linweb.gamepad.read() returns [[type, code, value], ...], or null once it is unplugged.
JSValue js_gamepad_open(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_gamepad_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

// Opens the first gamepad found in /dev/input and returns its name, or '' when there is none.
std::string open_gamepad();

} // namespace linweb
