#pragma once
#include <string>
#include <vector>

namespace linweb {
namespace nodelite {

// Runs scripts with node-lite (/usr/bin/node-lite) in the background, so the UI keeps drawing while a
// script waits for the network. Output (stdout and stderr together) is read with poll().

struct StartResult {
    bool ok = false;
    int id = 0;
    std::string error;
};

struct PollResult {
    bool found = false;
    bool running = false;
    bool timed_out = false;
    int exit_code = 0;
    std::string output;   // everything so far; the last 64 KiB when there is more
};

// True when the node-lite program can be found.
bool available();

// Directories a script file may live in (default /root/node-lite, /root/ps3, /mnt and /tmp).
void set_script_roots(const std::vector<std::string>& roots);

// Starts `script` with args. The script must exist inside a script root; it runs in its own directory.
// A job that runs longer than timeout_ms (default one minute) is killed.
StartResult start_file(const std::string& script, const std::vector<std::string>& args, int timeout_ms = 60000);

// Starts a snippet of JavaScript (saved to a temporary file that is removed when the job ends).
StartResult start_eval(const std::string& code, int timeout_ms = 60000);

PollResult poll(int id);

// Kills the job if it still runs and forgets it.
void stop(int id);
void stop_all();

} // namespace nodelite
} // namespace linweb
