#include "key_event.h"

namespace linweb {

namespace {

// Keysym numbers are the X11 ones (XK_*), which xkbcommon uses too.
const uint32_t kBackSpace = 0xff08, kTab = 0xff09, kLinefeed = 0xff0a, kReturn = 0xff0d, kEscape = 0xff1b,
               kDelete = 0xffff, kHome = 0xff50, kLeft = 0xff51, kUp = 0xff52, kRight = 0xff53, kDown = 0xff54,
               kPrior = 0xff55, kNext = 0xff56, kEnd = 0xff57, kBegin = 0xff58, kInsert = 0xff63,
               kPause = 0xff13, kScrollLock = 0xff14, kPrint = 0xff61, kMenu = 0xff67, kNumLock = 0xff7f,
               kModeSwitch = 0xff7e, kISOLevel3 = 0xfe03, kISOLeftTab = 0xfe20,
               kShiftL = 0xffe1, kShiftR = 0xffe2, kControlL = 0xffe3, kControlR = 0xffe4, kCapsLock = 0xffe5,
               kShiftLock = 0xffe6, kMetaL = 0xffe7, kMetaR = 0xffe8, kAltL = 0xffe9, kAltR = 0xffea,
               kSuperL = 0xffeb, kSuperR = 0xffec, kHyperL = 0xffed, kHyperR = 0xffee,
               kKPEnter = 0xff8d, kKPHome = 0xff95, kKPDown = 0xff99, kKPEnd = 0xff9c, kKPInsert = 0xff9e,
               kKPDelete = 0xff9f, kKPMultiply = 0xffaa, kKPAdd = 0xffab, kKPSubtract = 0xffad,
               kKPDecimal = 0xffae, kKPDivide = 0xffaf, kKP0 = 0xffb0, kKP9 = 0xffb9, kKPEqual = 0xffbd,
               kKPSpace = 0xff80, kF1 = 0xffbe, kF35 = 0xffe0,
               kDeadFirst = 0xfe50, kDeadLast = 0xfe5f, kDeadGrave = 0xfe50, kDeadAcute = 0xfe51,
               kDeadCircumflex = 0xfe52, kDeadTilde = 0xfe53, kDeadDiaeresis = 0xfe57;

bool is_modifier(uint32_t ks) {
    return (ks >= kShiftL && ks <= kHyperR) || ks == kISOLevel3 || ks == kModeSwitch || ks == kNumLock;
}

uint32_t codepoint_from_keysym(uint32_t ks) {
    if ((ks >= 0x20 && ks <= 0x7e) || (ks >= 0xa0 && ks <= 0xff)) return ks;
    if ((ks & 0xff000000u) == 0x01000000u) return ks & 0x00ffffffu;
    if (ks == 0x20ac) return 0x20ac;   // EuroSign
    return 0;
}

// Name of the keys that do not produce a character.
const char* special_name(uint32_t ks) {
    switch (ks) {
        case kBackSpace: return "Backspace";
        case kTab: case kISOLeftTab: return "Tab";
        case kReturn: case kLinefeed: case kKPEnter: return "Enter";
        case kEscape: return "Escape";
        case kDelete: case kKPDelete: return "Delete";
        case kHome: case kKPHome: return "Home";
        case kLeft: case 0xff96: return "ArrowLeft";
        case kUp: case 0xff97: return "ArrowUp";
        case kRight: case 0xff98: return "ArrowRight";
        case kDown: case kKPDown: return "ArrowDown";
        case kPrior: case 0xff9a: return "PageUp";
        case kNext: case 0xff9b: return "PageDown";
        case kEnd: case kKPEnd: return "End";
        case kBegin: case 0xff9d: return "Clear";
        case kInsert: case kKPInsert: return "Insert";
        case kPause: return "Pause";
        case kScrollLock: return "ScrollLock";
        case kPrint: return "PrintScreen";
        case kMenu: return "ContextMenu";
        case kNumLock: return "NumLock";
        case kShiftL: case kShiftR: return "Shift";
        case kControlL: case kControlR: return "Control";
        case kCapsLock: case kShiftLock: return "CapsLock";
        case kAltL: case kAltR: return "Alt";
        case kISOLevel3: case kModeSwitch: return "AltGraph";
        case kMetaL: case kMetaR: case kSuperL: case kSuperR: case kHyperL: case kHyperR: return "Meta";
        case 0x1008ff12: return "AudioVolumeMute";
        case 0x1008ff11: return "AudioVolumeDown";
        case 0x1008ff13: return "AudioVolumeUp";
        case 0x1008ff14: return "MediaPlayPause";
        case 0x1008ff15: return "MediaStop";
        case 0x1008ff16: return "MediaTrackPrevious";
        case 0x1008ff17: return "MediaTrackNext";
        case 0x1008ff02: return "BrightnessUp";
        case 0x1008ff03: return "BrightnessDown";
        default: return nullptr;
    }
}

std::string key_name(uint32_t ks) {
    if (const char* special = special_name(ks)) return special;
    if (ks >= kF1 && ks <= kF35) return "F" + std::to_string(ks - kF1 + 1);
    if (ks >= kKP0 && ks <= kKP9) return std::string(1, static_cast<char>('0' + (ks - kKP0)));
    switch (ks) {
        case kKPMultiply: return "*";
        case kKPAdd: return "+";
        case kKPSubtract: return "-";
        case kKPDecimal: return ".";
        case kKPDivide: return "/";
        case kKPEqual: return "=";
        case kKPSpace: return " ";
        default: break;
    }
    if (ks >= kDeadFirst && ks <= kDeadLast) return "Dead";
    uint32_t cp = codepoint_from_keysym(ks);
    return cp ? utf8_from_codepoint(cp) : "Unidentified";
}

Key legacy_key(uint32_t ks) {
    if (ks >= 'a' && ks <= 'z') return static_cast<Key>(static_cast<int>(Key::A) + (ks - 'a'));
    if (ks >= 'A' && ks <= 'Z') return static_cast<Key>(static_cast<int>(Key::A) + (ks - 'A'));
    if (ks >= '0' && ks <= '9') return static_cast<Key>(static_cast<int>(Key::Num0) + (ks - '0'));
    switch (ks) {
        case kReturn: case kLinefeed: case kKPEnter: return Key::Enter;
        case kEscape: return Key::Escape;
        case ' ': return Key::Space;
        case kBackSpace: return Key::Backspace;
        case kLeft: return Key::Left;
        case kUp: return Key::Up;
        case kRight: return Key::Right;
        case kDown: return Key::Down;
        case kTab: return Key::Tab;
        case kShiftL: return Key::LeftShift;
        case kShiftR: return Key::RightShift;
        case kControlL: return Key::LeftControl;
        case kControlR: return Key::RightControl;
        case kAltL: return Key::LeftAlt;
        case kAltR: return Key::RightAlt;
        case kF1 + 1: return Key::F2;
        case kF1 + 2: return Key::F3;
        default: return Key::Unknown;
    }
}

// DOM keyCode. Punctuation and the number row follow the physical position (evdev code), so
// they stay the same on every layout; letters follow the layout, as in a browser.
int dom_key_code(int evdev, uint32_t base, uint32_t ks) {
    switch (evdev) {
        case 39: return 186;  // ;
        case 13: return 187;  // =
        case 51: return 188;  // ,
        case 12: return 189;  // -
        case 52: return 190;  // .
        case 53: return 191;  // /
        case 41: return 192;  // `
        case 26: return 219;  // [
        case 43: return 220;  // backslash
        case 27: return 221;  // ]
        case 40: return 222;  // '
        case 86: return 226;  // <> key
        default: break;
    }
    if (evdev >= 2 && evdev <= 10) return 49 + (evdev - 2);
    if (evdev == 11) return 48;
    if (base >= 'a' && base <= 'z') return 'A' + (base - 'a');
    if (base >= 'A' && base <= 'Z') return base;
    if (ks >= kKP0 && ks <= kKP9) return 96 + static_cast<int>(ks - kKP0);
    if (ks >= kF1 && ks <= kF1 + 23) return 112 + static_cast<int>(ks - kF1);
    switch (ks) {
        case kBackSpace: return 8;
        case kTab: case kISOLeftTab: return 9;
        case kReturn: case kLinefeed: case kKPEnter: return 13;
        case kEscape: return 27;
        case ' ': return 32;
        case kPrior: case 0xff9a: return 33;
        case kNext: case 0xff9b: return 34;
        case kEnd: case kKPEnd: return 35;
        case kHome: case kKPHome: return 36;
        case kLeft: case 0xff96: return 37;
        case kUp: case 0xff97: return 38;
        case kRight: case 0xff98: return 39;
        case kDown: case kKPDown: return 40;
        case kPrint: return 44;
        case kInsert: case kKPInsert: return 45;
        case kDelete: case kKPDelete: return 46;
        case kShiftL: case kShiftR: return 16;
        case kControlL: case kControlR: return 17;
        case kAltL: case kAltR: case kISOLevel3: case kModeSwitch: return 18;
        case kPause: return 19;
        case kCapsLock: case kShiftLock: return 20;
        case kSuperL: case kMetaL: return 91;
        case kSuperR: case kMetaR: return 92;
        case kMenu: return 93;
        case kKPMultiply: return 106;
        case kKPAdd: return 107;
        case kKPSubtract: return 109;
        case kKPDecimal: return 110;
        case kKPDivide: return 111;
        case kNumLock: return 144;
        case kScrollLock: return 145;
        default: return 0;
    }
}

// DOM "code": the physical key, from the evdev key code.
std::string dom_code(int evdev) {
    static const char* const kLetters[] = {"KeyQ", "KeyW", "KeyE", "KeyR", "KeyT", "KeyY", "KeyU", "KeyI", "KeyO", "KeyP"};
    static const char* const kHomeRow[] = {"KeyA", "KeyS", "KeyD", "KeyF", "KeyG", "KeyH", "KeyJ", "KeyK", "KeyL"};
    static const char* const kBottom[] = {"KeyZ", "KeyX", "KeyC", "KeyV", "KeyB", "KeyN", "KeyM"};
    static const char* const kPad[] = {"Numpad7", "Numpad8", "Numpad9", "", "Numpad4", "Numpad5", "Numpad6", "",
                                       "Numpad1", "Numpad2", "Numpad3", "Numpad0", "NumpadDecimal"};
    if (evdev >= 2 && evdev <= 10) return "Digit" + std::to_string(evdev - 1);
    if (evdev == 11) return "Digit0";
    if (evdev >= 16 && evdev <= 25) return kLetters[evdev - 16];
    if (evdev >= 30 && evdev <= 38) return kHomeRow[evdev - 30];
    if (evdev >= 44 && evdev <= 50) return kBottom[evdev - 44];
    if (evdev >= 59 && evdev <= 68) return "F" + std::to_string(evdev - 58);
    if (evdev >= 71 && evdev <= 83 && kPad[evdev - 71][0]) return kPad[evdev - 71];
    if (evdev >= 183 && evdev <= 194) return "F" + std::to_string(evdev - 170);
    switch (evdev) {
        case 1: return "Escape";
        case 12: return "Minus";
        case 13: return "Equal";
        case 14: return "Backspace";
        case 15: return "Tab";
        case 26: return "BracketLeft";
        case 27: return "BracketRight";
        case 28: return "Enter";
        case 29: return "ControlLeft";
        case 39: return "Semicolon";
        case 40: return "Quote";
        case 41: return "Backquote";
        case 42: return "ShiftLeft";
        case 43: return "Backslash";
        case 51: return "Comma";
        case 52: return "Period";
        case 53: return "Slash";
        case 54: return "ShiftRight";
        case 55: return "NumpadMultiply";
        case 56: return "AltLeft";
        case 57: return "Space";
        case 58: return "CapsLock";
        case 69: return "NumLock";
        case 70: return "ScrollLock";
        case 74: return "NumpadSubtract";
        case 78: return "NumpadAdd";
        case 86: return "IntlBackslash";
        case 87: return "F11";
        case 88: return "F12";
        case 96: return "NumpadEnter";
        case 97: return "ControlRight";
        case 98: return "NumpadDivide";
        case 99: return "PrintScreen";
        case 100: return "AltRight";
        case 102: return "Home";
        case 103: return "ArrowUp";
        case 104: return "PageUp";
        case 105: return "ArrowLeft";
        case 106: return "ArrowRight";
        case 107: return "End";
        case 108: return "ArrowDown";
        case 109: return "PageDown";
        case 110: return "Insert";
        case 111: return "Delete";
        case 113: return "AudioVolumeMute";
        case 114: return "AudioVolumeDown";
        case 115: return "AudioVolumeUp";
        case 119: return "Pause";
        case 125: return "MetaLeft";
        case 126: return "MetaRight";
        case 127: return "ContextMenu";
        case 163: return "MediaTrackNext";
        case 164: return "MediaPlayPause";
        case 165: return "MediaTrackPrevious";
        case 166: return "MediaStop";
        case 224: return "BrightnessDown";
        case 225: return "BrightnessUp";
        default: return "Unidentified";
    }
}

struct Combo { uint32_t base; uint32_t result; };

const Combo kAcute[] = {{'A', 0xc1}, {'E', 0xc9}, {'I', 0xcd}, {'O', 0xd3}, {'U', 0xda}, {'Y', 0xdd},
                        {'a', 0xe1}, {'e', 0xe9}, {'i', 0xed}, {'o', 0xf3}, {'u', 0xfa}, {'y', 0xfd}};
const Combo kGrave[] = {{'A', 0xc0}, {'E', 0xc8}, {'I', 0xcc}, {'O', 0xd2}, {'U', 0xd9},
                        {'a', 0xe0}, {'e', 0xe8}, {'i', 0xec}, {'o', 0xf2}, {'u', 0xf9}};
const Combo kCircumflex[] = {{'A', 0xc2}, {'E', 0xca}, {'I', 0xce}, {'O', 0xd4}, {'U', 0xdb},
                             {'a', 0xe2}, {'e', 0xea}, {'i', 0xee}, {'o', 0xf4}, {'u', 0xfb}};
const Combo kDiaeresis[] = {{'A', 0xc4}, {'E', 0xcb}, {'I', 0xcf}, {'O', 0xd6}, {'U', 0xdc},
                            {'a', 0xe4}, {'e', 0xeb}, {'i', 0xef}, {'o', 0xf6}, {'u', 0xfc}, {'y', 0xff}};
const Combo kTilde[] = {{'A', 0xc3}, {'N', 0xd1}, {'O', 0xd5}, {'a', 0xe3}, {'n', 0xf1}, {'o', 0xf5}};

template <size_t N>
uint32_t combine(const Combo (&table)[N], uint32_t base) {
    for (const Combo& c : table) {
        if (c.base == base) return c.result;
    }
    return 0;
}

// What a dead key prints on its own (pressed twice, or followed by space).
uint32_t standalone(uint32_t dead) {
    switch (dead) {
        case kDeadAcute: return 0xb4;
        case kDeadGrave: return '`';
        case kDeadCircumflex: return '^';
        case kDeadTilde: return '~';
        case kDeadDiaeresis: return 0xa8;
        default: return 0;
    }
}

} // namespace

std::string utf8_from_codepoint(uint32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xc0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xe0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    } else {
        out += static_cast<char>(0xf0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    }
    return out;
}

KeyEvent make_key_event(Action action, int evdev_code, int mods, uint32_t keysym, uint32_t base_keysym) {
    KeyEvent ev;
    ev.action = action;
    ev.scancode = evdev_code;
    ev.mods = mods;
    ev.repeat = action == Action::Repeat;
    ev.key = legacy_key(keysym);
    ev.name = key_name(keysym);
    ev.code = dom_code(evdev_code);
    ev.keyCode = dom_key_code(evdev_code, base_keysym, keysym);
    return ev;
}

void DeadKeys::process(uint32_t keysym, KeyEvent& ev) {
    if (keysym >= kDeadFirst && keysym <= kDeadLast) {
        if (pending_ == keysym) {                       // dead key twice: print it
            uint32_t accent = standalone(keysym);
            if (accent) ev.name = utf8_from_codepoint(accent);
            pending_ = 0;
        } else {
            pending_ = keysym;
        }
        return;
    }
    if (pending_ == 0 || is_modifier(keysym)) return;
    uint32_t dead = pending_;
    pending_ = 0;
    uint32_t cp = codepoint_from_keysym(keysym);
    if (!cp) return;
    uint32_t result = 0;
    if (cp == ' ') {
        result = standalone(dead);
    } else {
        switch (dead) {
            case kDeadAcute: result = combine(kAcute, cp); break;
            case kDeadGrave: result = combine(kGrave, cp); break;
            case kDeadCircumflex: result = combine(kCircumflex, cp); break;
            case kDeadDiaeresis: result = combine(kDiaeresis, cp); break;
            case kDeadTilde: result = combine(kTilde, cp); break;
            default: break;
        }
    }
    if (result) ev.name = utf8_from_codepoint(result);
}

} // namespace linweb
