#include "system/input.h"
#include <fstream>

namespace linweb {
namespace input {

std::vector<Device> list_devices(const std::string& proc_path) {
    std::vector<Device> devices;
    std::ifstream in(proc_path);
    std::string line;
    Device current;
    bool open = false;
    auto flush = [&]() {
        if (open) devices.push_back(current);
        current = Device();
        open = false;
    };
    while (std::getline(in, line)) {
        if (line.empty()) {
            flush();
        } else if (line.compare(0, 3, "N: ") == 0) {
            open = true;
            size_t first = line.find('"');
            size_t last = line.rfind('"');
            if (first != std::string::npos && last > first) current.name = line.substr(first + 1, last - first - 1);
        } else if (line.compare(0, 3, "H: ") == 0) {
            open = true;
            size_t eq = line.find('=');
            current.handlers = eq == std::string::npos ? std::string() : line.substr(eq + 1);
            while (!current.handlers.empty() && current.handlers.back() == ' ') current.handlers.pop_back();
        } else if (line.compare(0, 6, "B: ABS") == 0) {
            current.axes = true;
        }
    }
    flush();
    return devices;
}

} // namespace input
} // namespace linweb
