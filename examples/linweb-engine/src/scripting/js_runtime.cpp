#include "scripting/js_runtime.h"

namespace linweb {

JSRuntimeWrapper::JSRuntimeWrapper() : rt(nullptr) {}

JSRuntimeWrapper::~JSRuntimeWrapper() {
    destroy();
}

void JSRuntimeWrapper::create() {
    if (!rt) {
        rt = JS_NewRuntime();
    }
}

void JSRuntimeWrapper::destroy() {
    if (rt) {
        JS_FreeRuntime(rt);
        rt = nullptr;
    }
}

void JSRuntimeWrapper::set_memory_limit(size_t limit) {
    if (rt) {
        JS_SetMemoryLimit(rt, limit);
    }
}

void JSRuntimeWrapper::set_max_stack(size_t max_stack_size) {
    if (rt) {
        JS_SetMaxStackSize(rt, max_stack_size);
    }
}

} // namespace linweb
