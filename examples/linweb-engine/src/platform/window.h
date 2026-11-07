#pragma once
#include "platform_config.h"
#include "keys.h"
#include "key_event.h"
#include <string>

namespace linweb {

class Window;

using ScrollCallback = void (*)(Window*, double xoffset, double yoffset);
using KeyCallback = void (*)(Window*, Key key, int scancode, Action action, int mods);
using KeyEventCallback = void (*)(Window*, const KeyEvent& event);
using MouseButtonCallback = void (*)(Window*, MouseButton button, Action action, int mods);
using CursorPosCallback = void (*)(Window*, double x, double y);

class Window {
public:
    Window();
    ~Window();

    bool init(int width = 800, int height = 600, const std::string& title = "Linweb");
    void shutdown();
    bool should_close() const;
    void swap_buffers() const;
    void poll_events() const;

    void get_framebuffer_size(int& width, int& height) const;
    void get_window_size(int& width, int& height) const;
    void get_cursor_pos(double& x, double& y) const;
    bool is_mouse_button_pressed(MouseButton button) const;
    double get_time() const;

    void set_scroll_callback(ScrollCallback callback);
    void set_key_callback(KeyCallback callback);
    void set_key_event_callback(KeyEventCallback callback) { key_event_cb_ = callback; }
    void set_mouse_button_callback(MouseButtonCallback callback);
    void set_cursor_pos_callback(CursorPosCallback callback);

    ScrollCallback get_scroll_callback() const { return scroll_cb_; }
    KeyCallback get_key_callback() const { return key_cb_; }
    KeyEventCallback get_key_event_callback() const { return key_event_cb_; }

    // Delivers a keyboard event to the handlers: the built-in shortcuts (legacy callback)
    // and the full DOM-style event.
    void emit_key(const KeyEvent& event) {
        if (key_cb_) key_cb_(this, event.key, event.scancode, event.action, event.mods);
        if (key_event_cb_) key_event_cb_(this, event);
    }
    MouseButtonCallback get_mouse_button_callback() const { return mouse_btn_cb_; }
    CursorPosCallback get_cursor_pos_callback() const { return cursor_pos_cb_; }

    void make_context_current();
    void enable_blend();

    // Internal use for handlers
    void* get_native_window() const;

    struct Impl;

private:
    Impl* impl_;
    int width_;
    int height_;
    std::string title_;

    ScrollCallback scroll_cb_;
    KeyCallback key_cb_;
    KeyEventCallback key_event_cb_ = nullptr;
    MouseButtonCallback mouse_btn_cb_;
    CursorPosCallback cursor_pos_cb_;
};

} // namespace linweb
