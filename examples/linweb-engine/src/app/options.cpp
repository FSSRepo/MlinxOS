#include "app/options.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include "utils/file_utils.h"
#include "utils/log.h"

namespace linweb {

bool parse_options(int argc, char** argv, Options& options)
{
    bool file_specified = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--screenshot-live") {
            options.static_mode = false;
        } else if (arg.compare(0, 19, "--screenshot-delay=") == 0) {
            try { options.screenshot_delay = std::max(0.0, std::stod(arg.substr(19))); } catch (...) { std::cerr << "Invalid screenshot delay\n"; return false; }
        } else if (arg.compare(0, 7, "--size=") == 0) {
            // --size=WxH sets the window (and so the viewport) size; the default is 800x600
            std::string size = arg.substr(7);
            size_t x = size.find('x');
            try {
                options.window_width = std::stoi(size.substr(0, x));
                options.window_height = std::stoi(size.substr(x + 1));
            } catch (...) { std::cerr << "Invalid size, use --size=WxH: " << size << std::endl; return false; }
        } else if (arg.compare(0, 14, "--max-texture=") == 0) {
            // --max-texture=N caps the longest side of a texture on the GPU; 0 uses the driver limit
            int side = -1;
            try { side = std::stoi(arg.substr(14)); } catch (...) {}
            if (side < 0) { std::cerr << "Invalid max texture, use --max-texture=N: " << arg.substr(14) << std::endl; return false; }
            options.max_texture_side = side;
        } else if (arg.compare(0, 17, "--texture-budget=") == 0) {
            // --texture-budget=MB is the memory the large textures may take before the least recently used ones are freed
            int megabytes = -1;
            try { megabytes = std::stoi(arg.substr(17)); } catch (...) {}
            if (megabytes < 0) { std::cerr << "Invalid texture budget, use --texture-budget=MB: " << arg.substr(17) << std::endl; return false; }
            options.texture_budget_mb = static_cast<size_t>(megabytes);
        } else if (arg == "--fps") {
            options.show_fps = true;
        } else if (arg == "--profiler") {
            options.show_profiler = true;
        } else if (arg.compare(0, 7, "--keys=") == 0) {
            // --keys=ArrowRight@1.5,Enter@3 presses those keys (DOM names) at those seconds; it needs the scripts running
            std::stringstream list(arg.substr(7));
            for (std::string item; std::getline(list, item, ',');) {
                size_t at = item.find('@');
                try { options.scripted_keys.push_back({item.substr(0, at), at == std::string::npos ? 0.0 : std::stod(item.substr(at + 1))}); }
                catch (...) { std::cerr << "Invalid key, use --keys=Name@seconds: " << item << std::endl; return false; }
            }
            options.static_mode = false;
        } else if (arg.compare(0, 14, "--layout-dump=") == 0) {
            options.layout_dump_path = arg.substr(14);
        } else if (arg.compare(0, 6, "--log=") == 0) {
            if (!log_open(arg.substr(6))) {
                std::cerr << "Could not open the log: " << arg.substr(6) << std::endl;
                return false;
            }
        } else if (arg.compare(0, 11, "--user-css=") == 0) {
            options.user_css_path = arg.substr(11);
        } else if (arg == "--static") {
            options.static_mode = true;
        } else if (arg.compare(0, 13, "--screenshot=") == 0) {
            options.screenshot_path = arg.substr(13);
            options.static_mode = true;
        } else if (arg == "--debug") {
            options.debug_mode = DebugType::Element;
        } else if (arg == "--debug=element") {
            options.debug_mode = DebugType::Element;
        } else if (arg == "--debug=text") {
            options.debug_mode = DebugType::Text;
        } else if (arg == "--debug=dimens") {
            options.debug_mode = DebugType::Dimens;
        } else if (arg == "--debug=anim") {
            options.debug_mode = DebugType::Anim;
        } else if (!file_specified) {
            options.html_file = arg;
            file_specified = true;
            if (!file_exists(options.html_file)) {
                std::string alt_path = "assets/pages/" + options.html_file;
                if (file_exists(alt_path)) {
                    options.html_file = alt_path;
                } else {
                    std::string exe_relative = get_executable_dir() + "/" + options.html_file;
                    if (file_exists(exe_relative)) {
                        options.html_file = exe_relative;
                    }
                }
            }
        }
    }

    if (!options.scripted_keys.empty()) options.static_mode = false;   // the keys need the scripts
    return true;
}

} // namespace linweb
