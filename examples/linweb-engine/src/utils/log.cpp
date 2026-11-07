#include "utils/log.h"

#include <chrono>
#include <fstream>
#include <iomanip>

namespace linweb {

namespace {

std::ofstream g_log;
const std::chrono::steady_clock::time_point g_start = std::chrono::steady_clock::now();

} // namespace

bool log_open(const std::string& path) {
    g_log.open(path, std::ios::out | std::ios::trunc);
    return g_log.is_open();
}

bool log_enabled() {
    return g_log.is_open();
}

void log_event(const std::string& category, const std::string& message) {
    if (!g_log.is_open()) return;
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
    g_log << '[' << std::fixed << std::setprecision(3) << seconds << "] " << category << ": " << message << '\n' << std::flush;
}

void log_close() {
    g_log.close();
}

} // namespace linweb
