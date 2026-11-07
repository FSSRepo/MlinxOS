#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace linweb {
namespace gamepad {

// One input_event from /dev/input: type (EV_KEY, EV_ABS, ...), code (BTN_*, ABS_*) and value.
struct Event {
    uint16_t type;
    uint16_t code;
    int32_t value;
};

// Opens an evdev node such as /dev/input/event11 without blocking. Closes any previous one.
bool open_device(const std::string& dev_path);
void close_device();
bool is_open();

// Every event waiting on the open device. Never blocks; returns nothing when no device is open.
std::vector<Event> read_events();

} // namespace gamepad
} // namespace linweb
