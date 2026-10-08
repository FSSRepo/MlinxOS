#pragma once
#include "input_handler.h"
#include "keys.h"
#include <string>

namespace linweb {

class Window;
class JSEngine;

class KeyboardHandler : public InputHandler {
public:
    KeyboardHandler();
    ~KeyboardHandler() override = default;

    void setup(Window* window) override;
    void update() override;

    void handle_key(Window* window, Key key, int scancode, Action action, int mods);

    void set_js_engine(JSEngine* js_engine) { js_engine_ = js_engine; }

    static void key_callback(Window* window, Key key, int scancode, Action action, int mods);

private:
    JSEngine* js_engine_;
};

} // namespace linweb
