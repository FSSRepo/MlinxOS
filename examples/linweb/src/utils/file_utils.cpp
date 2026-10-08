#include "file_utils.h"
#include <fstream>
#include <sstream>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

namespace linweb {

std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::vector<std::string> read_file_lines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream file(path);
    if (!file.is_open()) return lines;
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    return lines;
}

bool file_exists(const std::string& path) {
    std::ifstream file(path);
    return file.good();
}

std::string get_file_extension(const std::string& path) {
    size_t dot_pos = path.find_last_of('.');
    if (dot_pos == std::string::npos) return "";
    return path.substr(dot_pos + 1);
}

std::string get_executable_dir() {
#ifdef _WIN32
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string full_path(path);
    size_t pos = full_path.find_last_of("\\/");
    if (pos != std::string::npos) {
        return full_path.substr(0, pos);
    }
#endif
    return ".";
}

std::string get_directory(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos != std::string::npos) {
        return path.substr(0, pos);
    }
    return ".";
}

std::string resolve_resource_path(const std::string& base_dir, const std::string& resource_path) {
    std::string resolved;

    // Try relative to base directory of the HTML file
    if (!base_dir.empty() && base_dir != ".") {
        resolved = base_dir + "/" + resource_path;
        if (file_exists(resolved)) return resolved;
    }

    // Try relative to current directory
    if (file_exists(resource_path)) return resource_path;

    // Fall back to assets/pages/
    resolved = "assets/pages/" + resource_path;
    if (file_exists(resolved)) return resolved;

    // Not found, return the original path
    return resource_path;
}

} // namespace linweb
