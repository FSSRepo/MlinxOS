#include "system/power.h"
#include <cerrno>
#include <cstring>
#include <sys/reboot.h>
#include <unistd.h>

namespace linweb {
namespace power {

Result power_off() {
    sync();
    Result result;
    if (reboot(RB_POWER_OFF) != 0) {
        result.error = std::string("power off: ") + std::strerror(errno);
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace power
} // namespace linweb
