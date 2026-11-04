#pragma once
#include "quickjs.h"

namespace linweb {

// Block devices and filesystems for scripts (exposed as linweb.storage).
JSValue js_storage_list(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_storage_mount(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_storage_unmount(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_storage_format(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_storage_list_dir(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_storage_read_file(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_storage_write_file(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_storage_make_dir(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_storage_remove(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

} // namespace linweb
