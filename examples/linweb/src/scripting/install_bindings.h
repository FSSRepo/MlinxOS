#pragma once
#include "quickjs.h"

namespace linweb {

// Installer for scripts: linweb.system.install(disk, confirm) returns { ok, error } and starts the
// install; linweb.system.installStatus() returns { running, done, exitCode, log }.
JSValue js_install_start(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_install_status(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
// linweb.system.runsFromDisk(): true once the system runs from an installed disk.
JSValue js_runs_from_disk(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
// linweb.system.installAvailable(): true when the image includes the installer.
JSValue js_install_available(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

} // namespace linweb
