#pragma once
#include "app/module.h"

// The native part of the OS around the Linweb engine: storage, network, node-lite, installer, power, system information, the
// gamepad and the shader backgrounds of the demo. The engine knows nothing about it; it only calls these hooks.
class SystemOsModule : public linweb::Module {
public:
    const char* name() const override { return "linweb-system-os"; }
    void install_js(linweb::JSEngine& js) override;
    void on_page_loaded(const std::string& html_base_dir) override;
    void on_frame(linweb::KeyboardHandler& keyboard) override;
    bool wants_frame() const override;
    void on_shutdown() override;
};
