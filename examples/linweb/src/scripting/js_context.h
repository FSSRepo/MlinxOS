#pragma once
#include "scripting/js_runtime.h"
#include <string>

namespace linweb {

class JSContextWrapper {
public:
    JSContextWrapper();
    ~JSContextWrapper();

    void create(JSRuntimeWrapper& runtime);
    void destroy();

    bool evaluate(const std::string& code, const std::string& filename = "<input>");
    void register_function(const std::string& name, JSCFunction* func);
    bool call_function(const std::string& name);

    JSContext* get() const { return ctx; }

private:
    JSContext* ctx;
};

} // namespace linweb
