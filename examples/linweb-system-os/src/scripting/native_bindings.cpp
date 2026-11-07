#include "scripting/native_bindings.h"
#include "natives.h"

namespace linweb {

// The registration lives in natives.cpp now (register_system_natives); this keeps the old entry point of the module
// working for anything that still includes scripting/native_bindings.h.
void install_native_bindings(JSEngine& js) {
    register_system_natives(js);
}

} // namespace linweb
