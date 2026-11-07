#include "utils/memory_files.h"
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace linweb {
namespace memory_files {

namespace {

const char* const kMarker = "/.linweb-mem/";

std::mutex g_mutex;
std::unordered_map<std::string, std::shared_ptr<const std::string>> g_files;
std::atomic<int> g_next_directory{1};

// "C:\work\.linweb-mem\1\a.png" and "/.linweb-mem/1/a.png" are the same file: the key starts at the marker.
// Empty when the path is not a virtual one.
std::string key_of(const std::string& path) {
    std::string normalized = path;
    for (char& c : normalized) if (c == '\\') c = '/';
    const size_t at = normalized.find(kMarker);
    return at == std::string::npos ? std::string() : normalized.substr(at);
}

} // namespace

std::string new_directory() {
    return std::string(kMarker) + std::to_string(g_next_directory++);
}

void put(const std::string& path, std::string bytes) {
    const std::string key = key_of(path);
    if (key.empty()) return;
    auto value = std::make_shared<const std::string>(std::move(bytes));
    std::lock_guard<std::mutex> lock(g_mutex);
    g_files[key] = std::move(value);
}

std::shared_ptr<const std::string> find(const std::string& path) {
    const std::string key = key_of(path);
    if (key.empty()) return nullptr;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto found = g_files.find(key);
    return found == g_files.end() ? nullptr : found->second;
}

} // namespace memory_files
} // namespace linweb
