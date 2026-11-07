#pragma once
#include <string>
#include <vector>

namespace linweb {
namespace sysinfo {

struct Mount {
    std::string device;
    std::string mountpoint;
    std::string type;
    bool read_only = false;
};

struct Memory {
    long total_kb = 0;
    long free_kb = 0;
    long available_kb = 0;
    long cached_kb = 0;
};

// Every mount of the system, from /proc/mounts (the live system: the squashfs, the overlay, the medium).
std::vector<Mount> mounts(const std::string& proc_mounts = "/proc/mounts");

// Memory figures in kB, from /proc/meminfo.
Memory memory(const std::string& proc_meminfo = "/proc/meminfo");

} // namespace sysinfo
} // namespace linweb
