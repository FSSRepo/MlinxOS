#include "scripting/network_bindings.h"
#include "system/network.h"
#include <string>
#include <vector>

namespace linweb {

namespace {

std::string arg_string(JSContext* ctx, int argc, JSValueConst* argv, int index) {
    if (index >= argc) return std::string();
    size_t len = 0;
    const char* text = JS_ToCStringLen(ctx, &len, argv[index]);
    std::string out = text ? std::string(text, len) : std::string();
    if (text) JS_FreeCString(ctx, text);
    return out;
}

int arg_int(JSContext* ctx, int argc, JSValueConst* argv, int index, int fallback) {
    int32_t value = fallback;
    if (index < argc && JS_ToInt32(ctx, &value, argv[index]) != 0) return fallback;
    return value;
}

void set_str(JSContext* ctx, JSValue obj, const char* key, const std::string& value) {
    JS_SetPropertyStr(ctx, obj, key, JS_NewStringLen(ctx, value.data(), value.size()));
}

void set_bool(JSContext* ctx, JSValue obj, const char* key, bool value) {
    JS_SetPropertyStr(ctx, obj, key, JS_NewBool(ctx, value));
}

void set_num(JSContext* ctx, JSValue obj, const char* key, double value) {
    JS_SetPropertyStr(ctx, obj, key, JS_NewFloat64(ctx, value));
}

JSValue result_object(JSContext* ctx, const net::Result& result) {
    JSValue obj = JS_NewObject(ctx);
    set_bool(ctx, obj, "ok", result.ok);
    set_str(ctx, obj, "error", result.error);
    return obj;
}

JSValue address_array(JSContext* ctx, const std::vector<net::Address>& addresses) {
    JSValue array = JS_NewArray(ctx);
    for (size_t i = 0; i < addresses.size(); ++i) {
        JSValue obj = JS_NewObject(ctx);
        set_str(ctx, obj, "iface", addresses[i].iface);
        set_str(ctx, obj, "ip", addresses[i].ip);
        JS_SetPropertyUint32(ctx, array, static_cast<uint32_t>(i), obj);
    }
    return array;
}

} // namespace

JSValue js_wifi_status(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    net::WifiStatus st = net::wifi_status();
    JSValue obj = JS_NewObject(ctx);
    set_bool(ctx, obj, "available", st.available);
    set_str(ctx, obj, "iface", st.iface);
    set_str(ctx, obj, "state", st.state);
    set_str(ctx, obj, "ssid", st.ssid);
    set_str(ctx, obj, "ip", st.ip);
    set_str(ctx, obj, "error", st.error);
    return obj;
}

JSValue js_wifi_enable(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return result_object(ctx, net::wifi_enable());
}

JSValue js_wifi_scan(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return result_object(ctx, net::wifi_scan());
}

JSValue js_wifi_networks(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    std::vector<net::WifiNetwork> networks = net::wifi_networks();
    JSValue array = JS_NewArray(ctx);
    for (size_t i = 0; i < networks.size(); ++i) {
        JSValue obj = JS_NewObject(ctx);
        set_str(ctx, obj, "ssid", networks[i].ssid);
        set_num(ctx, obj, "signal", networks[i].signal_percent);
        set_num(ctx, obj, "dbm", networks[i].signal_dbm);
        set_num(ctx, obj, "frequency", networks[i].frequency);
        set_bool(ctx, obj, "secure", networks[i].secure);
        set_str(ctx, obj, "security", networks[i].security);
        set_bool(ctx, obj, "connected", networks[i].connected);
        JS_SetPropertyUint32(ctx, array, static_cast<uint32_t>(i), obj);
    }
    return array;
}

JSValue js_wifi_connect(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return result_object(ctx, net::wifi_connect(arg_string(ctx, argc, argv, 0), arg_string(ctx, argc, argv, 1)));
}

JSValue js_wifi_disconnect(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return result_object(ctx, net::wifi_disconnect());
}

JSValue js_net_addresses(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return address_array(ctx, net::local_addresses());
}

JSValue js_webserver_status(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    net::WebServerStatus st = net::webserver_status();
    JSValue obj = JS_NewObject(ctx);
    set_bool(ctx, obj, "running", st.running);
    set_num(ctx, obj, "port", st.port);
    set_str(ctx, obj, "error", st.error);
    JS_SetPropertyStr(ctx, obj, "addresses", address_array(ctx, st.addresses));
    return obj;
}

JSValue js_webserver_start(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return result_object(ctx, net::webserver_start(arg_int(ctx, argc, argv, 0, 8080)));
}

JSValue js_webserver_stop(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return result_object(ctx, net::webserver_stop());
}

} // namespace linweb
