#pragma once
#include <string>

namespace linweb {
struct LayoutBox;
// Capture the current OpenGL back buffer as a 24-bit BMP, without window chrome.
bool capture_frame(const std::string& path, int width, int height);
bool dump_layout(const std::string& path, const LayoutBox& root);
}
