#pragma once
#include <string>
#include <vector>

namespace linweb {
namespace input {

struct Device {
    std::string name;      // e.g. "USB Input Device"
    std::string handlers;  // e.g. "kbd event3" (event nodes under /dev/input)
    bool axes = false;     // has absolute axes: sticks and triggers, so likely a gamepad
};

// Input devices the kernel reports in /proc/bus/input/devices.
std::vector<Device> list_devices(const std::string& proc_path = "/proc/bus/input/devices");

} // namespace input
} // namespace linweb
