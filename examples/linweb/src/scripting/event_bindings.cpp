#include "scripting/event_bindings.h"
#include "scripting/js_engine.h"

namespace linweb {

extern JSEngine* g_js_engine;

void dispatch_event(const std::string& target, const std::string& event_type, const std::string& event_data) {
    if (g_js_engine) {
        g_js_engine->dispatch_event(target, event_type, event_data);
    }
}

} // namespace linweb
