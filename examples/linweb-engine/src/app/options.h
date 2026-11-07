#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include "renderer/renderer.h"

namespace linweb {

// A key press the command line schedules (--keys=ArrowRight@1.5,Enter@3): the DOM key name and the second to press it at.
struct ScriptedKey {
    std::string name;
    double at = 0.0;
};

// What the command line asks for.
struct Options {
    DebugType debug_mode = DebugType::None;
    std::string html_file = "assets/pages/index.html";
    bool static_mode = false;           // the scripts do not run (--static, and --screenshot unless --screenshot-live follows)
    std::string screenshot_path;
    double screenshot_delay = 0.0;
    std::string layout_dump_path;
    std::string user_css_path;
    int window_width = 800, window_height = 600;
    int max_texture_side = 0;           // --max-texture=N: the longest side of a texture on the GPU; 0 keeps the default
    size_t texture_budget_mb = 0;       // --texture-budget=MB: the memory the large textures may take; 0 keeps the default
    std::vector<ScriptedKey> scripted_keys;
    bool show_fps = false;
    bool show_profiler = false;
};

// Reads the arguments into `options`. Returns false, after printing why, when one of them is not valid.
bool parse_options(int argc, char** argv, Options& options);

} // namespace linweb
