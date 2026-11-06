#include "system/install.h"
#include "system/process.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace linweb {
namespace install {

namespace {

const char* kLog = "/tmp/mlinx-install.log";
const char* kDone = "/tmp/mlinx-install.done";
pid_t g_pid = 0;

std::string base_name(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string read_file(const char* file) {
    std::ifstream in(file);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

Result failure(const std::string& message) {
    Result result;
    result.error = message;
    return result;
}

} // namespace

Result start(const std::string& disk, const std::string& confirm) {
    if (status().running) return failure("an install is already running");
    if (disk.compare(0, 5, "/dev/") != 0 || disk.find("..") != std::string::npos) {
        return failure("the disk must be a device under /dev");
    }
    if (confirm != base_name(disk)) return failure("type " + base_name(disk) + " to confirm");

    // Start from an empty log and no result: the status of a previous install must not show up here.
    std::ofstream(kLog, std::ios::trunc).close();
    ::unlink(kDone);

    // `sh -c` runs the installer with its output and exit code redirected to files that status() reads.
    pid_t pid = process::spawn({"sh", "-c",
        "exec /usr/sbin/mlinx-install \"$0\" > /tmp/mlinx-install.log 2>&1; echo $? > /tmp/mlinx-install.done",
        disk});
    if (pid < 0) return failure("could not start mlinx-install");
    g_pid = pid;
    Result result;
    result.ok = true;
    return result;
}

Status status() {
    Status result;
    result.log = read_file(kLog);
    std::string done = read_file(kDone);
    if (!done.empty()) {
        result.done = true;
        result.exit_code = std::atoi(done.c_str());
        if (g_pid > 0) {
            waitpid(g_pid, nullptr, 0);   // the shell is already exiting: reap it
            g_pid = 0;
        }
    }
    result.running = g_pid > 0 && !result.done;
    return result;
}

bool available() {
    return ::access("/usr/sbin/mlinx-install", X_OK) == 0;
}

bool runs_from_disk() {
    std::istringstream lines(read_file("/proc/mounts"));
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string device, mountpoint, type;
        fields >> device >> mountpoint >> type;
        if (mountpoint == "/") return type != "rootfs" && type != "ramfs" && type != "tmpfs";
    }
    return false;
}

} // namespace install
} // namespace linweb
