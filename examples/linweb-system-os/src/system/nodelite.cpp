#include "system/nodelite.h"

#ifdef __linux__
#include "system/process.h"

#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace linweb {
namespace nodelite {

namespace {

constexpr size_t kMaxOutput = 64 * 1024;
constexpr size_t kMaxCode = 64 * 1024;
constexpr size_t kMaxArgs = 16;
constexpr size_t kMaxArgLength = 512;
constexpr size_t kMaxJobs = 8;
constexpr size_t kMaxRunning = 4;

struct Job {
    pid_t pid = -1;
    int fd = -1;                 // read end of the output pipe, -1 once it hit end of file
    std::string output;
    bool done = false;
    bool timed_out = false;
    int exit_code = 0;
    std::string temp_file;
    std::chrono::steady_clock::time_point deadline;
};

std::map<int, Job> g_jobs;
int g_next_id = 1;
std::vector<std::string> g_roots = {"/root/node-lite", "/root/ps3", "/mnt", "/tmp"};

std::string real_path(const std::string& path) {
    char buffer[PATH_MAX];
    return realpath(path.c_str(), buffer) ? std::string(buffer) : std::string();
}

bool inside_roots(const std::string& resolved) {
    for (const std::string& root : g_roots) {
        std::string real_root = real_path(root);
        if (real_root.empty()) continue;
        if (resolved.size() > real_root.size() && resolved.compare(0, real_root.size(), real_root) == 0 &&
            resolved[real_root.size()] == '/') {
            return true;
        }
    }
    return false;
}

void drain(Job& job) {
    while (job.fd >= 0) {
        char buffer[4096];
        ssize_t n = read(job.fd, buffer, sizeof(buffer));
        if (n > 0) {
            job.output.append(buffer, static_cast<size_t>(n));
            if (job.output.size() > kMaxOutput) job.output.erase(0, job.output.size() - kMaxOutput);
        } else if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
            close(job.fd);
            job.fd = -1;
        } else if (errno == EAGAIN) {
            return;
        }
    }
}

void finish(Job& job, int status) {
    job.done = true;
    job.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    drain(job);
    if (job.fd >= 0) { close(job.fd); job.fd = -1; }
    if (!job.temp_file.empty()) { unlink(job.temp_file.c_str()); job.temp_file.clear(); }
}

void kill_job(Job& job) {
    if (job.done) return;
    kill(-job.pid, SIGKILL);
    int status = 0;
    waitpid(job.pid, &status, 0);
    finish(job, status);
}

void update(Job& job) {
    if (job.done) return;
    drain(job);
    int status = 0;
    if (waitpid(job.pid, &status, WNOHANG) == job.pid) {
        finish(job, status);
    } else if (std::chrono::steady_clock::now() >= job.deadline) {
        kill_job(job);
        job.timed_out = true;
        job.exit_code = -1;
    }
}

void forget(std::map<int, Job>::iterator it) {
    kill_job(it->second);
    if (it->second.fd >= 0) close(it->second.fd);
    if (!it->second.temp_file.empty()) unlink(it->second.temp_file.c_str());
    g_jobs.erase(it);
}

// Makes room for a new job: forgets the oldest finished one, fails when too many scripts are running.
bool make_room(std::string& error) {
    size_t running = 0;
    for (auto& entry : g_jobs) {
        update(entry.second);
        if (!entry.second.done) ++running;
    }
    if (running >= kMaxRunning) { error = "too many scripts are running"; return false; }
    while (g_jobs.size() >= kMaxJobs) {
        auto oldest = g_jobs.end();
        for (auto it = g_jobs.begin(); it != g_jobs.end(); ++it) {
            if (it->second.done) { oldest = it; break; }
        }
        if (oldest == g_jobs.end()) { error = "too many scripts are running"; return false; }
        forget(oldest);
    }
    return true;
}

StartResult launch(const std::string& program, const std::vector<std::string>& argv_strings,
                   const std::string& cwd, const std::string& temp_file, int timeout_ms) {
    StartResult result;
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        result.error = std::string("pipe failed: ") + std::strerror(errno);
        if (!temp_file.empty()) unlink(temp_file.c_str());
        return result;
    }

    std::vector<char*> argv;
    for (const std::string& arg : argv_strings) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        result.error = std::string("fork failed: ") + std::strerror(errno);
        close(fds[0]); close(fds[1]);
        if (!temp_file.empty()) unlink(temp_file.c_str());
        return result;
    }
    if (pid == 0) {
        setpgid(0, 0);
        int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd >= 0) dup2(null_fd, 0);
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) { /* run from where we are */ }
        execv(program.c_str(), argv.data());
        _exit(127);
    }
    setpgid(pid, pid);
    close(fds[1]);
    fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);

    Job job;
    job.pid = pid;
    job.fd = fds[0];
    job.temp_file = temp_file;
    job.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 60000);
    result.id = g_next_id++;
    g_jobs.emplace(result.id, std::move(job));
    result.ok = true;
    return result;
}

bool check_args(const std::vector<std::string>& args, std::string& error) {
    if (args.size() > kMaxArgs) { error = "too many arguments"; return false; }
    for (const std::string& arg : args) {
        if (arg.size() > kMaxArgLength) { error = "an argument is too long"; return false; }
    }
    return true;
}

} // namespace

bool available() {
    return !process::find_tool("node-lite").empty();
}

void set_script_roots(const std::vector<std::string>& roots) {
    g_roots = roots;
}

StartResult start_file(const std::string& script, const std::vector<std::string>& args, int timeout_ms) {
    StartResult result;
    std::string program = process::find_tool("node-lite");
    if (program.empty()) { result.error = "node-lite is not installed in this system"; return result; }
    if (!check_args(args, result.error)) return result;

    std::string resolved = real_path(script);
    if (resolved.empty()) { result.error = "script not found: " + script; return result; }
    if (!inside_roots(resolved)) { result.error = "scripts must be inside /root/node-lite, /root/ps3, /mnt or /tmp"; return result; }
    if (!make_room(result.error)) return result;

    std::vector<std::string> argv = {program, resolved};
    argv.insert(argv.end(), args.begin(), args.end());
    return launch(program, argv, resolved.substr(0, resolved.rfind('/')), std::string(), timeout_ms);
}

StartResult start_eval(const std::string& code, int timeout_ms) {
    StartResult result;
    std::string program = process::find_tool("node-lite");
    if (program.empty()) { result.error = "node-lite is not installed in this system"; return result; }
    if (code.empty() || code.size() > kMaxCode) { result.error = "the code is empty or longer than 64 KiB"; return result; }
    if (!make_room(result.error)) return result;

    char name[] = "/tmp/linweb-node-XXXXXX";
    int fd = mkstemp(name);
    if (fd < 0) { result.error = std::string("cannot create a temporary file: ") + std::strerror(errno); return result; }
    size_t written = 0;
    while (written < code.size()) {
        ssize_t n = write(fd, code.data() + written, code.size() - written);
        if (n <= 0) break;
        written += static_cast<size_t>(n);
    }
    close(fd);
    if (written != code.size()) { unlink(name); result.error = "cannot write the temporary file"; return result; }

    struct stat st;
    std::string cwd = stat("/root/node-lite", &st) == 0 && S_ISDIR(st.st_mode) ? "/root/node-lite" : "/";
    return launch(program, {program, name}, cwd, name, timeout_ms);
}

PollResult poll(int id) {
    PollResult result;
    auto it = g_jobs.find(id);
    if (it == g_jobs.end()) return result;
    update(it->second);
    result.found = true;
    result.running = !it->second.done;
    result.timed_out = it->second.timed_out;
    result.exit_code = it->second.exit_code;
    result.output = it->second.output;
    return result;
}

void stop(int id) {
    auto it = g_jobs.find(id);
    if (it != g_jobs.end()) forget(it);
}

void stop_all() {
    while (!g_jobs.empty()) forget(g_jobs.begin());
}

} // namespace nodelite
} // namespace linweb

#else // !__linux__

namespace linweb {
namespace nodelite {
bool available() { return false; }
void set_script_roots(const std::vector<std::string>&) {}
StartResult start_file(const std::string&, const std::vector<std::string>&, int) {
    StartResult result;
    result.error = "not supported on this platform";
    return result;
}
StartResult start_eval(const std::string&, int) {
    StartResult result;
    result.error = "not supported on this platform";
    return result;
}
PollResult poll(int) { return PollResult(); }
void stop(int) {}
void stop_all() {}
} // namespace nodelite
} // namespace linweb

#endif
