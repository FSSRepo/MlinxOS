#include "system/gamepad.h"

#ifdef __linux__
#include <cerrno>
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>

namespace linweb {
namespace gamepad {

namespace {

int g_fd = -1;

} // namespace

bool open_device(const std::string& dev_path) {
    close_device();
    g_fd = ::open(dev_path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    return g_fd >= 0;
}

void close_device() {
    if (g_fd >= 0) ::close(g_fd);
    g_fd = -1;
}

bool is_open() {
    return g_fd >= 0;
}

std::vector<Event> read_events() {
    std::vector<Event> events;
    if (g_fd < 0) return events;
    struct input_event buffer[32];
    for (;;) {
        ssize_t n = ::read(g_fd, buffer, sizeof(buffer));
        if (n < 0 && errno == ENODEV) {  // unplugged: the caller can open it again later
            close_device();
            break;
        }
        if (n <= 0) break;               // nothing pending (EAGAIN) or end of device
        const size_t count = static_cast<size_t>(n) / sizeof(buffer[0]);
        for (size_t i = 0; i < count; ++i) {
            events.push_back(Event{buffer[i].type, buffer[i].code, buffer[i].value});
        }
    }
    return events;
}

} // namespace gamepad
} // namespace linweb

#else // !__linux__

namespace linweb {
namespace gamepad {
bool open_device(const std::string&) { return false; }
void close_device() {}
bool is_open() { return false; }
std::vector<Event> read_events() { return {}; }
} // namespace gamepad
} // namespace linweb

#endif
