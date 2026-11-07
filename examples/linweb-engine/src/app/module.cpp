#include "app/module.h"
#include "app/app_state.h"
#include <vector>

namespace linweb {

namespace {
std::vector<std::unique_ptr<Module>>& modules() {
    static std::vector<std::unique_ptr<Module>> list;
    return list;
}
} // namespace

void register_module(std::unique_ptr<Module> module) {
    if (module) modules().push_back(std::move(module));
}

void toggle_profiler() {
    g_show_profiler = !g_show_profiler;
}

void modules_install_js(JSEngine& js) {
    for (auto& module : modules()) module->install_js(js);
}

void modules_on_page_loaded(const std::string& html_base_dir) {
    for (auto& module : modules()) module->on_page_loaded(html_base_dir);
}

void modules_on_frame(KeyboardHandler& keyboard) {
    for (auto& module : modules()) module->on_frame(keyboard);
}

bool modules_want_frame() {
    for (auto& module : modules()) {
        if (module->wants_frame()) return true;
    }
    return false;
}

void modules_shutdown() {
    for (auto& module : modules()) module->on_shutdown();
    modules().clear();
}

} // namespace linweb
