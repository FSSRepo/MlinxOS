#pragma once
#include <string>
#include <vector>
#ifdef __linux__
#include <sys/types.h>
#else
typedef int pid_t;
#endif

namespace linweb {
namespace process {

// Looks `name` up in $PATH, then in the standard bin directories. Empty when not found.
std::string find_tool(const std::string& name);

// Runs args[0] without a shell and captures stdout and stderr (the first 8 KiB). Returns false
// when the program is missing, cannot start or runs longer than timeout_ms (it is then killed);
// otherwise exit_code holds its exit status.
bool run_capture(const std::vector<std::string>& args, std::string& output, int& exit_code, int timeout_ms = 10000);

// Starts a program in the background with its output discarded. Returns its pid, or -1.
// With new_group it leads its own process group, so kill(-pid, ...) reaches its children.
pid_t spawn(const std::vector<std::string>& args, bool new_group = true);

} // namespace process
} // namespace linweb
