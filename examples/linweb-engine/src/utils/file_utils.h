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

// Folder of the page being shown. Set once at start-up (main), used to resolve image paths at draw time.
extern std::string g_html_base_dir;
// Path of an image source: relative sources are looked up from the page folder, so images created by
// scripts (which are not rewritten when the page is parsed) load like the ones written in the HTML.
std::string resolve_image_src(const std::string& src);

} // namespace linweb
