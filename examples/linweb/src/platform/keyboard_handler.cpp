#include "keyboard_handler.h"
#include "window.h"
#include "../debug/debug_visualizer.h"
#include "../renderer/renderer.h"
#include "scripting/js_engine.h"

namespace linweb {

// Global state
extern bool g_show_node_tree;
extern bool g_debug_mode_active;
extern std::shared_ptr<Node> g_debug_hovered_node;
extern JSEngine* g_js_engine;

static KeyboardHandler* g_keyboard_handler_instance = nullptr;

KeyboardHandler::KeyboardHandler() : js_engine_(nullptr) {
    g_keyboard_handler_instance = this;
}

void KeyboardHandler::setup(Window* window) {
    if (window) {
        window->set_key_callback(key_callback);
    }
}

void KeyboardHandler::update() {
    // Keyboard is event-driven; nothing to do in update
}

void KeyboardHandler::handle_key(Window* window, Key key, int scancode, Action action, int mods) {
    if (key == Key::F2 && action == Action::Press) {
        g_show_node_tree = !g_show_node_tree;
    }
    if (key == Key::D) {
        if (action == Action::Press) g_debug_mode_active = true;
        else if (action == Action::Release) {
            g_debug_mode_active = false;
            g_debug_hovered_node = nullptr;
            Renderer::set_debug_box(nullptr);
        }
    }

    if (g_js_engine && (action == Action::Press || action == Action::Release || action == Action::Repeat)) {
        std::string key_name;
        int keyCode = 0;
        if (key >= Key::A && key <= Key::Z) {
            int offset = static_cast<int>(key) - static_cast<int>(Key::A);
            key_name = (char)('a' + offset);
            keyCode = (int)('A' + offset);
        }
        else if (key >= Key::Num0 && key <= Key::Num9) {
            int offset = static_cast<int>(key) - static_cast<int>(Key::Num0);
            key_name = (char)('0' + offset);
            keyCode = (int)('0' + offset);
        }
        else if (key == Key::Enter) { key_name = "Enter"; keyCode = 13; }
        else if (key == Key::Escape) { key_name = "Escape"; keyCode = 27; }
        else if (key == Key::Space) { key_name = " "; keyCode = 32; }
        else if (key == Key::Backspace) { key_name = "Backspace"; keyCode = 8; }
        else if (key == Key::Left) { key_name = "ArrowLeft"; keyCode = 37; }
        else if (key == Key::Up) { key_name = "ArrowUp"; keyCode = 38; }
        else if (key == Key::Right) { key_name = "ArrowRight"; keyCode = 39; }
        else if (key == Key::Down) { key_name = "ArrowDown"; keyCode = 40; }
        else if (key == Key::Tab) { key_name = "Tab"; keyCode = 9; }
        else if (key == Key::LeftShift || key == Key::RightShift) { key_name = "Shift"; keyCode = 16; }
        else if (key == Key::LeftControl || key == Key::RightControl) { key_name = "Control"; keyCode = 17; }
        else if (key == Key::LeftAlt || key == Key::RightAlt) { key_name = "Alt"; keyCode = 18; }

        if (!key_name.empty()) {
            std::string event_type = (action == Action::Release) ? "keyup" : "keydown";
            std::string event_data = "{ key: '" + key_name + "', keyCode: " + std::to_string(keyCode) + " }";
            g_js_engine->dispatch_event("document", event_type, event_data);
            g_js_engine->dispatch_event("window", event_type, event_data);
        }
    }
}

void KeyboardHandler::key_callback(Window* window, Key key, int scancode, Action action, int mods) {
    if (g_keyboard_handler_instance) {
        g_keyboard_handler_instance->handle_key(window, key, scancode, action, mods);
    }
}

} // namespace linweb
