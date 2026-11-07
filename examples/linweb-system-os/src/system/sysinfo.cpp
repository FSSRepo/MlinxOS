#include "system/sysinfo.h"
#include <fstream>
#include <sstream>

namespace linweb {
namespace sysinfo {

namespace {

// /proc/mounts escapes spaces and tabs in paths as octal sequences (\040, \011).
std::string unescape(const std::string& field) {
    std::string out;
    for (size_t i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && i + 3 < field.size()) {
            int value = (field[i + 1] - '0') * 64 + (field[i + 2] - '0') * 8 + (field[i + 3] - '0');
            out += static_cast<char>(value);
            i += 3;
        } else {
            out += field[i];
        }
    }
    return out;
}

// Value of a "Key:   1234 kB" line of /proc/meminfo, or 0.
long meminfo_value(const std::string& text, const std::string& key) {
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.compare(0, key.size() + 1, key + ":") != 0) continue;
        std::istringstream fields(line.substr(key.size() + 1));
        long value = 0;
        fields >> value;
        return value;
    }
    return 0;
}

} // namespace

std::vector<Mount> mounts(const std::string& proc_mounts) {
    std::vector<Mount> result;
    std::ifstream in(proc_mounts);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string device, mountpoint, type, options;
        fields >> device >> mountpoint >> type >> options;
        if (type.empty()) continue;
        Mount mount;
        mount.device = unescape(device);
        mount.mountpoint = unescape(mountpoint);
        mount.type = type;
        // The first option is "ro" or "rw".
        mount.read_only = options.compare(0, 2, "ro") == 0 && (options.size() == 2 || options[2] == ',');
        result.push_back(mount);
    }
    return result;
}

Memory memory(const std::string& proc_meminfo) {
    std::ifstream in(proc_meminfo);
    std::stringstream text;
    text << in.rdbuf();
    Memory memory;
    memory.total_kb = meminfo_value(text.str(), "MemTotal");
    memory.free_kb = meminfo_value(text.str(), "MemFree");
    memory.available_kb = meminfo_value(text.str(), "MemAvailable");
    memory.cached_kb = meminfo_value(text.str(), "Cached");
    return memory;
}

} // namespace sysinfo
} // namespace linweb
