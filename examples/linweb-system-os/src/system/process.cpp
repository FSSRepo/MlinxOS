#include "system/process.h"

#ifdef __linux__
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace linweb {
namespace process {

std::string find_tool(const std::string& name) {
    if (name.find('/') != std::string::npos) return access(name.c_str(), X_OK) == 0 ? name : std::string();
    std::vector<std::string> dirs;
    if (const char* path = std::getenv("PATH")) {
        std::stringstream ss(path);
        std::string dir;
        while (std::getline(ss, dir, ':')) {
            if (!dir.empty() && dir[0] == '/') dirs.push_back(dir + "/");
        }
    }
    for (const char* dir : {"/usr/bin/", "/bin/", "/usr/sbin/", "/sbin/"}) dirs.push_back(dir);
    for (const std::string& dir : dirs) {
        std::string candidate = dir + name;
        if (access(candidate.c_str(), X_OK) == 0) return candidate;
    }
    return std::string();
}

namespace {

std::vector<char*> to_argv(std::vector<std::string>& args) {
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(&a[0]);
    argv.push_back(nullptr);
    return argv;
}

} // namespace

bool run_capture(const std::vector<std::string>& args, std::string& output, int& exit_code, int timeout_ms) {
    output.clear();
    exit_code = -1;
    if (args.empty()) return false;
    std::string tool = find_tool(args[0]);
    if (tool.empty()) {
        output = args[0] + " is not installed in this system";
        return false;
    }
    int fds[2];
    if (pipe(fds) != 0) return false;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_addclose(&actions, fds[1]);
    std::vector<std::string> copy = args;
    std::vector<char*> argv = to_argv(copy);
    pid_t pid = 0;
    int rc = posix_spawn(&pid, tool.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (rc != 0) {
        close(fds[0]);
        output = std::strerror(rc);
        return false;
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 24 * 3600 * 1000);
    bool timed_out = false;
    char chunk[512];
    for (;;) {
        long left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) { timed_out = true; break; }
        struct pollfd pfd = {fds[0], POLLIN, 0};
        int ready = poll(&pfd, 1, static_cast<int>(left));
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) { timed_out = ready == 0; break; }
        ssize_t n = read(fds[0], chunk, sizeof(chunk));
        if (n <= 0) break;                       // EOF: the program closed its output
        if (output.size() < 8192) output.append(chunk, static_cast<size_t>(n));
    }
    close(fds[0]);
    if (timed_out) kill(pid, SIGKILL);
    int status = 0;
    waitpid(pid, &status, 0);
    if (timed_out) {
        output += output.empty() ? "timed out" : "\n(timed out)";
        return false;
    }
    exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return true;
}

pid_t spawn(const std::vector<std::string>& args, bool new_group) {
    if (args.empty()) return -1;
    std::string tool = find_tool(args[0]);
    if (tool.empty()) return -1;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    if (new_group) {
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&attr, 0);
    }
    std::vector<std::string> copy = args;
    std::vector<char*> argv = to_argv(copy);
    pid_t pid = 0;
    int rc = posix_spawn(&pid, tool.c_str(), &actions, &attr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    return rc == 0 ? pid : -1;
}

} // namespace process
} // namespace linweb

#else // !__linux__

namespace linweb {
namespace process {
std::string find_tool(const std::string&) { return std::string(); }
bool run_capture(const std::vector<std::string>&, std::string& output, int& exit_code, int) {
    output = "not supported on this platform";
    exit_code = -1;
    return false;
}
pid_t spawn(const std::vector<std::string>&, bool) { return -1; }
} // namespace process
} // namespace linweb

#endif
