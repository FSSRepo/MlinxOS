#pragma once
#include "quickjs.h"

namespace linweb {

// Network for scripts: Wi-Fi (linweb.wifi), the addresses of this machine and the web server
// (linweb.webserver).
JSValue js_wifi_status(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_wifi_enable(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_wifi_scan(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_wifi_networks(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_wifi_connect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_wifi_disconnect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_net_addresses(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_webserver_status(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_webserver_start(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_webserver_stop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

} // namespace linweb
