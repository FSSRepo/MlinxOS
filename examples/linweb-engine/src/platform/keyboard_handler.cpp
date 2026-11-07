#include "keyboard_handler.h"
#include <cstdio>
#include "window.h"
#include "../debug/debug_visualizer.h"
#include "../renderer/renderer.h"
#include "scripting/js_engine.h"
#include "forms/form_bindings.h"
#include "forms/form_controls.h"

namespace linweb {

// Global state
extern bool g_show_node_tree;
extern bool g_debug_mode_active;
extern bool g_show_profiler;
extern std::shared_ptr<Node> g_debug_hovered_node;
extern JSEngine* g_js_engine;

static KeyboardHandler* g_keyboard_handler_instance = nullptr;

KeyboardHandler::KeyboardHandler() : js_engine_(nullptr) {
    g_keyboard_handler_instance = this;
}

void KeyboardHandler::setup(Window* window) {
    if (window) {
        window->set_key_callback(key_callback);
        window->set_key_event_callback(key_event_callback);
    }
}

void KeyboardHandler::update() {
    // Keyboard is event-driven; nothing to do in update
}

void KeyboardHandler::handle_key(Window* window, Key key, int scancode, Action action, int mods) {
    if (key == Key::F2 && action == Action::Press) {
        g_show_node_tree = !g_show_node_tree;
    }
    if (key == Key::F3 && action == Action::Press) {
        g_show_profiler = !g_show_profiler;
    }
    if (key == Key::D) {
        if (action == Action::Press) g_debug_mode_active = true;
        else if (action == Action::Release) {
            g_debug_mode_active = false;
            g_debug_hovered_node = nullptr;
            Renderer::set_debug_box(nullptr);
        }
    }
}

void KeyboardHandler::key_callback(Window* window, Key key, int scancode, Action action, int mods) {
    if (g_keyboard_handler_instance) {
        g_keyboard_handler_instance->handle_key(window, key, scancode, action, mods);
    }
}

namespace {

// JavaScript string literal for the event data (quotes, backslashes and control characters escaped).
std::string js_quote(const std::string& text) {
    std::string out = "'";
    for (unsigned char c : text) {
        if (c == '\\' || c == '\'') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 0x20 || c == 0x7f) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\x%02x", c); out += buf; }
        else out += static_cast<char>(c);
    }
    return out + "'";
}

} // namespace

// Full keyboard event for scripts: same fields as DOM KeyboardEvent.
void KeyboardHandler::handle_key_event(Window*, const KeyEvent& ev) {
    if (!g_js_engine) return;
    std::string data = "{ key: " + js_quote(ev.name) + ", code: " + js_quote(ev.code) +
        ", keyCode: " + std::to_string(ev.keyCode) + ", which: " + std::to_string(ev.keyCode) +
        ", shiftKey: " + ((ev.mods & ModShift) ? "true" : "false") +
        ", ctrlKey: " + ((ev.mods & ModControl) ? "true" : "false") +
        ", altKey: " + ((ev.mods & ModAlt) ? "true" : "false") +
        ", metaKey: " + ((ev.mods & ModMeta) ? "true" : "false") +
        ", capsLock: " + ((ev.mods & ModCapsLock) ? "true" : "false") +
        ", repeat: " + (ev.repeat ? "true" : "false") +
        ", source: " + js_quote(ev.source) +
        ", preventDefault: function() { __preventDefault(); } }";
    std::string type = (ev.action == Action::Release) ? "keyup" : "keydown";
    g_js_engine->dispatch_event("document", type, data);
    g_js_engine->dispatch_event("window", type, data);

    // default action of the key on the focused form control, unless a script called preventDefault()
    if (ev.action != Action::Release && !forms::take_default_prevented()) {
        forms::Key key;
        key.key = ev.name;
        key.ctrl = (ev.mods & ModControl) != 0;
        key.alt = (ev.mods & ModAlt) != 0;
        key.shift = (ev.mods & ModShift) != 0;
        key.meta = (ev.mods & ModMeta) != 0;
        forms::handle_key(key);
    }
}

void KeyboardHandler::key_event_callback(Window* window, const KeyEvent& event) {
    if (g_keyboard_handler_instance) g_keyboard_handler_instance->handle_key_event(window, event);
}

} // namespace linweb
