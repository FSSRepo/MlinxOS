#include "gamepad_input.h"
#include "app/module.h"
#include "platform/key_event.h"
#include "platform/keyboard_handler.h"
#include "scripting/event_bindings.h"
#include "scripting/gamepad_bindings.h"
#include "system/gamepad.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace linweb {
namespace gamepad_input {

namespace {

// Linux event codes of the gamepad (the same ones the old ps3/js/gamepad.js used).
constexpr uint16_t EV_KEY = 1;
constexpr uint16_t EV_ABS = 3;
constexpr uint16_t ABS_X = 0;
constexpr uint16_t ABS_Y = 1;
constexpr uint16_t ABS_RX = 3;        // L2 as a trigger (0..127)
constexpr uint16_t ABS_RY = 4;        // R2 as a trigger (0..127)
constexpr uint16_t ABS_HAT0X = 16;
constexpr uint16_t ABS_HAT0Y = 17;
constexpr uint16_t BTN_CROSS = 304;   // BTN_A: X on a PS controller, accept
constexpr uint16_t BTN_TRIANGLE = 307; // BTN_X: triangle on a PS controller, back
constexpr uint16_t BTN_L1 = 308;      // the two buttons before L2 (310) / R2 (311) on this pad; not confirmed on the real one
constexpr uint16_t BTN_R1 = 309;
constexpr uint16_t BTN_R2 = 311;     // BTN_TR2: R2 sent as a button (profiler)
constexpr uint16_t BTN_L2 = 310;      // L2 sent as a button (the one before R2)
constexpr uint16_t BTN_L2_ALT = 312;  // BTN_TL2 of the standard layout
constexpr int TRIGGER_THRESHOLD = 64; // pressed past half of the travel
constexpr int STICK_THRESHOLD = 64;   // sticks report -127..127 around 0
constexpr std::chrono::seconds RETRY_INTERVAL{2};

struct KeyName {
    const char* name;   // DOM "key"
    const char* code;   // DOM "code"
    int keyCode;        // DOM legacy keyCode
};

constexpr KeyName ENTER{"Enter", "Enter", 13};
constexpr KeyName ESCAPE{"Escape", "Escape", 27};
constexpr KeyName ARROW_LEFT{"ArrowLeft", "ArrowLeft", 37};
constexpr KeyName ARROW_UP{"ArrowUp", "ArrowUp", 38};
constexpr KeyName ARROW_RIGHT{"ArrowRight", "ArrowRight", 39};
constexpr KeyName ARROW_DOWN{"ArrowDown", "ArrowDown", 40};
constexpr KeyName L2_KEY{"L2", "L2", 1002};   // not a keyboard key: the page returns to the music player with it
constexpr KeyName L1_KEY{"L1", "L1", 1000};   // shoulder buttons: the photo viewer changes picture with them
constexpr KeyName R1_KEY{"R1", "R1", 1001};
constexpr std::chrono::milliseconds L2_DEBOUNCE{250};
constexpr std::chrono::milliseconds HAT_REPEAT_DELAY{400};   // a held D-pad direction repeats after this...
constexpr std::chrono::milliseconds HAT_REPEAT_EVERY{90};    // ...and then at this pace
constexpr float STICK_DEAD_ZONE = 0.22f;                      // of the full travel
constexpr float STICK_RANGE = 127.0f;

struct State {
    bool was_open = false;
    std::string name;        // the device that is open, for the "gamepaddisconnected" event
    int hat[2] = {0, 0};     // hat x, y
    int stick[2] = {0, 0};   // left stick x, y
    float stick_raw[2] = {0.0f, 0.0f};   // the same, in -1..1
    bool axis_active = false;            // the last "gamepadaxis" event was not the centre
    std::chrono::steady_clock::time_point hat_next[2]{};
    int sent[2] = {0, 0};    // last direction sent per axis, so one push sends one key press
    bool r2_down = false;
    bool l2_axis_down = false;
    std::chrono::steady_clock::time_point last_l2{};
    std::chrono::steady_clock::time_point next_try{};
};

State g;

void press(KeyboardHandler& keyboard, const KeyName& key, const char* source = "", bool repeat = false) {
    KeyEvent ev;
    ev.action = Action::Press;
    ev.name = key.name;
    ev.code = key.code;
    ev.keyCode = key.keyCode;
    ev.repeat = repeat;
    ev.source = source;
    keyboard.handle_key_event(nullptr, ev);
}

// Sends the arrow of the axis when its direction changes (hat first, then stick).
void move_axis(KeyboardHandler& keyboard, int axis, const KeyName& negative, const KeyName& positive) {
    const bool from_hat = g.hat[axis] != 0;
    const int dir = from_hat ? g.hat[axis] : g.stick[axis];
    if (dir != 0 && dir != g.sent[axis]) {
        press(keyboard, dir < 0 ? negative : positive, from_hat ? "hat" : "stick");
        if (from_hat) g.hat_next[axis] = std::chrono::steady_clock::now() + HAT_REPEAT_DELAY;
    }
    g.sent[axis] = dir;
}

int sign_of(int value, int threshold) {
    if (value > threshold) return 1;
    if (value < -threshold) return -1;
    return 0;
}

// L2 reaches the page as the key "L2". Some pads send it as a button and as a trigger at once: one press, one key.
void press_l2(KeyboardHandler& keyboard) {
    const auto now = std::chrono::steady_clock::now();
    if (now - g.last_l2 < L2_DEBOUNCE) return;
    g.last_l2 = now;
    press(keyboard, L2_KEY);
}

void handle(KeyboardHandler& keyboard, const gamepad::Event& ev) {
    if (ev.type == EV_KEY && ev.value == 1) {
        if (ev.code == BTN_CROSS) press(keyboard, ENTER);
        else if (ev.code == BTN_TRIANGLE) press(keyboard, ESCAPE);
        else if (ev.code == BTN_L1) press(keyboard, L1_KEY);
        else if (ev.code == BTN_R1) press(keyboard, R1_KEY);
        else if (ev.code == BTN_R2) linweb::toggle_profiler();
        else if (ev.code == BTN_L2 || ev.code == BTN_L2_ALT) press_l2(keyboard);
    } else if (ev.type == EV_ABS) {
        if (ev.code == ABS_HAT0X) {
            g.hat[0] = ev.value > 0 ? 1 : ev.value < 0 ? -1 : 0;
            move_axis(keyboard, 0, ARROW_LEFT, ARROW_RIGHT);
        } else if (ev.code == ABS_HAT0Y) {
            g.hat[1] = ev.value > 0 ? 1 : ev.value < 0 ? -1 : 0;
            move_axis(keyboard, 1, ARROW_UP, ARROW_DOWN);
        } else if (ev.code == ABS_X) {
            g.stick_raw[0] = std::max(-1.0f, std::min(1.0f, static_cast<float>(ev.value) / STICK_RANGE));
            g.stick[0] = sign_of(ev.value, STICK_THRESHOLD);
            move_axis(keyboard, 0, ARROW_LEFT, ARROW_RIGHT);
        } else if (ev.code == ABS_Y) {
            g.stick_raw[1] = std::max(-1.0f, std::min(1.0f, static_cast<float>(ev.value) / STICK_RANGE));
            g.stick[1] = sign_of(ev.value, STICK_THRESHOLD);
            move_axis(keyboard, 1, ARROW_UP, ARROW_DOWN);
        } else if (ev.code == ABS_RX) {
            // L2 pressed (rising edge)
            const bool down = ev.value > TRIGGER_THRESHOLD;
            if (down && !g.l2_axis_down) press_l2(keyboard);
            g.l2_axis_down = down;
        } else if (ev.code == ABS_RY) {
            // R2 pressed (rising edge) toggles the profiler, the same as F3.
            const bool down = ev.value > TRIGGER_THRESHOLD;
            if (down && !g.r2_down) linweb::toggle_profiler();
            g.r2_down = down;
        }
    }
}

void forget_state() {
    g = State{};
}

// JavaScript string literal for a device name (quotes, backslashes and control characters escaped).
std::string js_quote(const std::string& text) {
    std::string out = "'";
    for (unsigned char c : text) {
        if (c == '\\' || c == '\'') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 0x20 || c == 0x7f) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\x%02x", c); out += buf; }
        else out += static_cast<char>(c);
    }
    return out + "'";
}

// window "gamepadconnected" / "gamepaddisconnected", with the fields of the DOM GamepadEvent that matter here:
// event.gamepad.id (the device name), .index and .connected.
void announce(const char* type, const std::string& name, bool connected) {
    dispatch_event("window", type,
                   "{ gamepad: { id: " + js_quote(name) + ", index: 0, connected: " + (connected ? "true" : "false") + " } }");
}

// A D-pad direction that stays pressed repeats its key, so holding Up zooms steadily
void repeat_hat(KeyboardHandler& keyboard) {
    const auto now = std::chrono::steady_clock::now();
    static const KeyName* const kNegative[2] = {&ARROW_LEFT, &ARROW_UP};
    static const KeyName* const kPositive[2] = {&ARROW_RIGHT, &ARROW_DOWN};
    for (int axis = 0; axis < 2; ++axis) {
        if (g.hat[axis] == 0 || now < g.hat_next[axis]) continue;
        g.hat_next[axis] = now + HAT_REPEAT_EVERY;
        press(keyboard, g.hat[axis] < 0 ? *kNegative[axis] : *kPositive[axis], "hat", true);
    }
}

// The stick as an analog pair for the page, on every frame while it is pushed
void send_stick_axis() {
    auto shaped = [](float v) {
        const float a = std::fabs(v);
        if (a < STICK_DEAD_ZONE) return 0.0f;
        const float scaled = (a - STICK_DEAD_ZONE) / (1.0f - STICK_DEAD_ZONE);
        return v < 0.0f ? -scaled : scaled;
    };
    const float x = shaped(g.stick_raw[0]);
    const float y = shaped(g.stick_raw[1]);
    const bool active = x != 0.0f || y != 0.0f;
    if (!active && !g.axis_active) return;
    g.axis_active = active;
    char data[64];
    std::snprintf(data, sizeof(data), "{ x: %.3f, y: %.3f }", x, y);
    dispatch_event("window", "gamepadaxis", data);
}

} // namespace

void poll(KeyboardHandler& keyboard) {
    const auto now = std::chrono::steady_clock::now();

    if (!gamepad::is_open()) {
        if (g.was_open) {
            // unplugged: clear what was held, tell the page and look for it again
            const std::string name = g.name;
            if (g.axis_active) dispatch_event("window", "gamepadaxis", "{ x: 0, y: 0 }");   // a pan must not go on
            forget_state();
            g.was_open = false;
            announce("gamepaddisconnected", name, false);
        }
        if (now < g.next_try) return;
        g.next_try = now + RETRY_INTERVAL;
        const std::string name = open_gamepad();
        if (name.empty()) return;
        g.was_open = true;
        g.name = name;
        announce("gamepadconnected", name, true);
        return;
    }

    for (const gamepad::Event& ev : gamepad::read_events()) handle(keyboard, ev);
    repeat_hat(keyboard);
    send_stick_axis();
}

bool active() {
    return g.axis_active || g.hat[0] != 0 || g.hat[1] != 0;
}

} // namespace gamepad_input
} // namespace linweb
