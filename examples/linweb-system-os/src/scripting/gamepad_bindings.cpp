#include "scripting/gamepad_bindings.h"
#include "system/gamepad.h"
#include "system/input.h"
#include <algorithm>
#include <cctype>

namespace linweb {

namespace {

// Event node of a device ("event11" among its handlers), or empty.
std::string event_node(const std::string& handlers) {
    size_t pos = 0;
    while (pos < handlers.size()) {
        size_t end = handlers.find(' ', pos);
        if (end == std::string::npos) end = handlers.size();
        std::string token = handlers.substr(pos, end - pos);
        if (token.compare(0, 5, "event") == 0) return token;
        pos = end + 1;
    }
    return std::string();
}

// True for a device named like a gamepad, so the VirtualBox tablet (which also has axes) is skipped.
bool looks_like_gamepad(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("pad") != std::string::npos || lower.find("joy") != std::string::npos ||
           lower.find("controller") != std::string::npos;
}

} // namespace

std::string open_gamepad() {
    for (const input::Device& device : input::list_devices()) {
        if (!device.axes || !looks_like_gamepad(device.name)) continue;
        std::string node = event_node(device.handlers);
        if (node.empty() || !gamepad::open_device("/dev/input/" + node)) continue;
        return device.name;
    }
    return std::string();
}

JSValue js_gamepad_open(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    std::string name = open_gamepad();
    return JS_NewStringLen(ctx, name.data(), name.size());
}

JSValue js_gamepad_read(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    if (!gamepad::is_open()) return JS_NULL;
    std::vector<gamepad::Event> events = gamepad::read_events();
    JSValue arr = JS_NewArray(ctx);
    uint32_t index = 0;
    for (const gamepad::Event& ev : events) {
        JSValue item = JS_NewArray(ctx);
        JS_SetPropertyUint32(ctx, item, 0, JS_NewInt32(ctx, ev.type));
        JS_SetPropertyUint32(ctx, item, 1, JS_NewInt32(ctx, ev.code));
        JS_SetPropertyUint32(ctx, item, 2, JS_NewInt32(ctx, ev.value));
        JS_SetPropertyUint32(ctx, arr, index++, item);
    }
    return arr;
}

} // namespace linweb
