#pragma once
#include <string>

namespace linweb {
namespace power {

struct Result {
    bool ok = false;
    std::string error;
};

// Flushes the disks and powers the machine off. Only returns when the kernel refuses (for
// example without root), and then error says why.
Result power_off();

} // namespace power
} // namespace linweb
