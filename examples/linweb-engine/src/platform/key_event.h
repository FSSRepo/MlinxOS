#pragma once
#include "keys.h"
#include <cstdint>
#include <string>

namespace linweb {

// Bits of KeyEvent::mods.
enum KeyMod : int {
    ModShift = 1,
    ModControl = 2,
    ModAlt = 4,
    ModMeta = 8,
    ModCapsLock = 16,
    ModNumLock = 32,
    ModAltGraph = 64,
};

// A keyboard event described the way DOM KeyboardEvent does. The platform windows build
// it from an X11 keysym (X11 and xkbcommon share the same keysym numbers), so one table
// maps the whole keyboard for Weston and Xorg.
struct KeyEvent {
    Action action = Action::Press;
    Key key = Key::Unknown;      // built-in shortcut id (F2, D...); Unknown for most keys
    int scancode = 0;            // evdev key code
    int mods = 0;                // KeyMod bits
    std::string name;            // DOM "key": "a", "A", "ñ", "Enter", "ArrowLeft", "F5"...
    std::string code;            // DOM "code": "KeyA", "Digit1", "ShiftLeft"...
    int keyCode = 0;             // DOM legacy keyCode (65 for A, 13 for Enter...)
    bool repeat = false;
    std::string source;          // "" for a keyboard; "hat" (D-pad) or "stick" for the keys a gamepad makes
};

// Builds the event for a key. `keysym` is the symbol with the current modifiers applied
// (shift, caps lock, AltGr), `base_keysym` the one the key produces with no modifiers.
KeyEvent make_key_event(Action action, int evdev_code, int mods, uint32_t keysym, uint32_t base_keysym);

std::string utf8_from_codepoint(uint32_t codepoint);

// Dead keys (acute, grave, circumflex, diaeresis, tilde) wait for the next letter and
// combine with it: dead acute then "a" gives "á", dead tilde then "n" gives "ñ".
class DeadKeys {
public:
    // Call for every press. Rewrites ev.name when a dead key is pending or being pressed.
    void process(uint32_t keysym, KeyEvent& ev);
    void reset() { pending_ = 0; }

private:
    uint32_t pending_ = 0;
};

} // namespace linweb
