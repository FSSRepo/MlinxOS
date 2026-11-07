#pragma once
#include <memory>
#include <string>

namespace linweb {
namespace memory_files {

// A page loaded from a URL is kept in memory, not in a folder: its files live under a virtual directory
// ("/.linweb-mem/<n>"). The readers of the engine (read_file, file_exists, textures, fonts, SVG) look here first.
// A path is recognised by that marker even after the filesystem functions made it absolute.

// A new virtual directory for one page.
std::string new_directory();

// Stores the bytes of a file; `path` is the virtual directory plus the name of the file.
void put(const std::string& path, std::string bytes);

// The bytes of a stored file, or null when `path` is not in memory. Safe to call from any thread.
std::shared_ptr<const std::string> find(const std::string& path);

} // namespace memory_files
} // namespace linweb
