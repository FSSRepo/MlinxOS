#pragma once
#include "quickjs.h"

namespace linweb {

class JSRuntimeWrapper {
public:
    JSRuntimeWrapper();
    ~JSRuntimeWrapper();

    void create();
    void destroy();
    void set_memory_limit(size_t limit);
    void set_max_stack(size_t max_stack_size);

    JSRuntime* get() const { return rt; }

private:
    JSRuntime* rt;
};

} // namespace linweb
