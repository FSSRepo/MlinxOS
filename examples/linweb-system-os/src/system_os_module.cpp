#include "system_os_module.h"
#include "gamepad_hook.h"
#include "natives.h"
#include "ps3_backgrounds.h"
#include "media/audio_output.h"
#include "system/network.h"
#include "system/nodelite.h"
#include "system/storage.h"
#include <filesystem>

void SystemOsModule::install_js(linweb::JSEngine& js) {
    register_system_natives(js);     // the `linweb` object of the scripts (natives.cpp)
    register_ps3_backgrounds();      // `background: ring` (ps3_backgrounds.cpp)
}

void SystemOsModule::on_page_loaded(const std::string& html_base_dir) {
    // the media elements of a page play the files that come with it, besides the ones on the mounted storage
    std::error_code ec;
    const std::filesystem::path page_dir = std::filesystem::absolute(html_base_dir.empty() ? "." : html_base_dir, ec);
    if (!ec) linweb::storage::set_extra_read_root(page_dir.lexically_normal().string());
}

void SystemOsModule::on_frame(linweb::KeyboardHandler& keyboard) {
    poll_gamepad(keyboard);
    // what the music is doing, for the ring background
    float level = 0.0f, bass = 0.0f;
    linweb::media::mixer_levels(level, bass);
    set_music_levels(level, bass);
}

bool SystemOsModule::wants_frame() const {
    return gamepad_wants_frame();
}

void SystemOsModule::on_shutdown() {
    linweb::net::webserver_stop();
    linweb::nodelite::stop_all();
}
