#include "platform/gamepad_input.h"
#include "platform/key_event.h"
#include "platform/keyboard_handler.h"
#include "scripting/gamepad_bindings.h"
#include "system/gamepad.h"
#include "system/perf.h"
#include <chrono>
#include <vector>

namespace linweb {
namespace gamepad_input {

namespace {

// Linux event codes of the gamepad (the same ones the old ps3/js/gamepad.js used).
constexpr uint16_t EV_KEY = 1;
constexpr uint16_t EV_ABS = 3;
constexpr uint16_t ABS_X = 0;
constexpr uint16_t ABS_Y = 1;
constexpr uint16_t ABS_RY = 4;        // R2 as a trigger (0..127)
constexpr uint16_t ABS_HAT0X = 16;
constexpr uint16_t ABS_HAT0Y = 17;
constexpr uint16_t BTN_CROSS = 304;   // BTN_A: X on a PS controller, accept
constexpr uint16_t BTN_TRIANGLE = 307; // BTN_X: triangle on a PS controller, back
constexpr uint16_t BTN_R2 = 311;      // BTN_TR2: R2 sent as a button (profiler)
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

struct State {
    bool was_open = false;
    int hat[2] = {0, 0};     // hat x, y
    int stick[2] = {0, 0};   // left stick x, y
    int sent[2] = {0, 0};    // last direction sent per axis, so one push sends one key press
    bool r2_down = false;
    std::chrono::steady_clock::time_point next_try{};
};

State g;

void press(KeyboardHandler& keyboard, const KeyName& key) {
    KeyEvent ev;
    ev.action = Action::Press;
    ev.name = key.name;
    ev.code = key.code;
    ev.keyCode = key.keyCode;
    keyboard.handle_key_event(nullptr, ev);
}

// Sends the arrow of the axis when its direction changes (hat first, then stick).
void move_axis(KeyboardHandler& keyboard, int axis, const KeyName& negative, const KeyName& positive) {
    const int dir = g.hat[axis] != 0 ? g.hat[axis] : g.stick[axis];
    if (dir != 0 && dir != g.sent[axis]) press(keyboard, dir < 0 ? negative : positive);
    g.sent[axis] = dir;
}

int sign_of(int value, int threshold) {
    if (value > threshold) return 1;
    if (value < -threshold) return -1;
    return 0;
}

void handle(KeyboardHandler& keyboard, const gamepad::Event& ev) {
    if (ev.type == EV_KEY && ev.value == 1) {
        if (ev.code == BTN_CROSS) press(keyboard, ENTER);
        else if (ev.code == BTN_TRIANGLE) press(keyboard, ESCAPE);
        else if (ev.code == BTN_R2) perf::toggle();
    } else if (ev.type == EV_ABS) {
        if (ev.code == ABS_HAT0X) {
            g.hat[0] = ev.value > 0 ? 1 : ev.value < 0 ? -1 : 0;
            move_axis(keyboard, 0, ARROW_LEFT, ARROW_RIGHT);
        } else if (ev.code == ABS_HAT0Y) {
            g.hat[1] = ev.value > 0 ? 1 : ev.value < 0 ? -1 : 0;
            move_axis(keyboard, 1, ARROW_UP, ARROW_DOWN);
        } else if (ev.code == ABS_X) {
            g.stick[0] = sign_of(ev.value, STICK_THRESHOLD);
            move_axis(keyboard, 0, ARROW_LEFT, ARROW_RIGHT);
        } else if (ev.code == ABS_Y) {
            g.stick[1] = sign_of(ev.value, STICK_THRESHOLD);
            move_axis(keyboard, 1, ARROW_UP, ARROW_DOWN);
        } else if (ev.code == ABS_RY) {
            // R2 pressed (rising edge) toggles the profiler, the same as F3.
            const bool down = ev.value > TRIGGER_THRESHOLD;
            if (down && !g.r2_down) perf::toggle();
            g.r2_down = down;
        }
    }
}

void forget_state() {
    g = State{};
}

} // namespace

void poll(KeyboardHandler& keyboard) {
    const auto now = std::chrono::steady_clock::now();

    if (!gamepad::is_open()) {
        if (g.was_open) {
            // unplugged: clear what was held and look for it again
            forget_state();
            g.was_open = false;
        }
        if (now < g.next_try) return;
        g.next_try = now + RETRY_INTERVAL;
        if (open_gamepad().empty()) return;
        g.was_open = true;
        return;
    }

    for (const gamepad::Event& ev : gamepad::read_events()) handle(keyboard, ev);
}

} // namespace gamepad_input
} // namespace linweb
