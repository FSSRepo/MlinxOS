#pragma once
#include "quickjs.h"

namespace linweb {

// System info for scripts: linweb.system.mounts() returns [{ device, mountpoint, type, readOnly }],
// and linweb.system.memory() returns { totalMB, availableMB, cachedMB, freeMB }.
JSValue js_system_mounts(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_system_memory(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

} // namespace linweb
