#pragma once
#include <string>
#include <vector>

namespace linweb {

std::string read_file(const std::string& path);
std::vector<std::string> read_file_lines(const std::string& path);
bool file_exists(const std::string& path);
std::string get_file_extension(const std::string& path);
std::string get_executable_dir();
std::string get_directory(const std::string& path);
std::string resolve_resource_path(const std::string& base_dir, const std::string& resource_path);

} // namespace linweb
