#pragma once
#include "scripting/js_runtime.h"
#include "scripting/js_context.h"
#include <string>

namespace linweb {

class JSEngine {
public:
    JSEngine();
    ~JSEngine();

    void execute(const std::string& code);
    void add_function(const std::string& name, JSCFunction* func);
    void call_function(const std::string& name);
    void dispatch_event(const std::string& target_class, const std::string& event_type, const std::string& event_data = "{}");

private:
    JSEngine(const JSEngine&) = delete;
    JSEngine& operator=(const JSEngine&) = delete;

    JSRuntimeWrapper runtime;
    JSContextWrapper context;
};

} // namespace linweb
