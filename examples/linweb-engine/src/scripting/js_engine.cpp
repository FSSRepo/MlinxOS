#include "scripting/js_engine.h"

namespace linweb {

JSEngine::JSEngine() {
    runtime.create();
    context.create(runtime);
}

JSEngine::~JSEngine() {
    context.destroy();
    runtime.destroy();
}

void JSEngine::execute(const std::string& code) {
    context.evaluate(code);
}

void JSEngine::add_function(const std::string& name, JSCFunction* func) {
    context.register_function(name, func);
}

void JSEngine::call_function(const std::string& name) {
    context.call_function(name);
}

void JSEngine::dispatch_event(const std::string& target_class, const std::string& event_type, const std::string& event_data) {
    std::string code = "if (window.dispatchEvent) window.dispatchEvent('" + target_class + "', '" + event_type + "', " + event_data + ");";
    execute(code);
}

} // namespace linweb
