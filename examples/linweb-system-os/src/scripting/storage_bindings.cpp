#include "scripting/storage_bindings.h"
#include "system/storage.h"
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

int64_t arg_int(JSContext* ctx, int argc, JSValueConst* argv, int index) {
    int64_t value = 0;
    if (index < argc && JS_ToInt64(ctx, &value, argv[index]) != 0) return 0;
    return value;
}

JSValue js_str(JSContext* ctx, const std::string& value) {
    return JS_NewStringLen(ctx, value.data(), value.size());
}

void set_str(JSContext* ctx, JSValue obj, const char* key, const std::string& value) {
    JS_SetPropertyStr(ctx, obj, key, js_str(ctx, value));
}

void set_bool(JSContext* ctx, JSValue obj, const char* key, bool value) {
    JS_SetPropertyStr(ctx, obj, key, JS_NewBool(ctx, value));
}

JSValue result_object(JSContext* ctx, const storage::Result& result) {
    JSValue obj = JS_NewObject(ctx);
    set_bool(ctx, obj, "ok", result.ok);
    set_str(ctx, obj, "error", result.error);
    return obj;
}

JSValue partition_object(JSContext* ctx, const storage::Partition& part) {
    JSValue obj = JS_NewObject(ctx);
    set_str(ctx, obj, "name", part.name);
    set_str(ctx, obj, "path", part.path);
    set_str(ctx, obj, "fstype", part.fstype);
    set_str(ctx, obj, "label", part.label);
    set_str(ctx, obj, "mountpoint", part.mountpoint);
    set_bool(ctx, obj, "mounted", !part.mountpoint.empty());
    JS_SetPropertyStr(ctx, obj, "sizeBytes", JS_NewInt64(ctx, static_cast<int64_t>(part.size_bytes)));
    return obj;
}

JSValue disk_object(JSContext* ctx, const storage::Disk& disk) {
    JSValue obj = JS_NewObject(ctx);
    set_str(ctx, obj, "name", disk.name);
    set_str(ctx, obj, "path", disk.path);
    set_str(ctx, obj, "model", disk.model);
    set_str(ctx, obj, "fstype", disk.fstype);
    set_str(ctx, obj, "label", disk.label);
    set_str(ctx, obj, "mountpoint", disk.mountpoint);
    set_bool(ctx, obj, "mounted", !disk.mountpoint.empty());
    set_bool(ctx, obj, "removable", disk.removable);
    set_bool(ctx, obj, "readOnly", disk.read_only);
    set_bool(ctx, obj, "optical", disk.optical);
    JS_SetPropertyStr(ctx, obj, "sizeBytes", JS_NewInt64(ctx, static_cast<int64_t>(disk.size_bytes)));
    JSValue parts = JS_NewArray(ctx);
    for (size_t i = 0; i < disk.partitions.size(); ++i) {
        JS_SetPropertyUint32(ctx, parts, static_cast<uint32_t>(i), partition_object(ctx, disk.partitions[i]));
    }
    JS_SetPropertyStr(ctx, obj, "partitions", parts);
    return obj;
}

} // namespace

JSValue js_storage_list(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    std::vector<storage::Disk> disks = storage::list_disks();
    JSValue array = JS_NewArray(ctx);
    for (size_t i = 0; i < disks.size(); ++i) {
        JS_SetPropertyUint32(ctx, array, static_cast<uint32_t>(i), disk_object(ctx, disks[i]));
    }
    return array;
}

JSValue js_storage_mount(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    std::string mountpoint;
    storage::Result result = storage::mount_device(arg_string(ctx, argc, argv, 0), mountpoint);
    JSValue obj = result_object(ctx, result);
    set_str(ctx, obj, "mountpoint", mountpoint);
    return obj;
}

JSValue js_storage_unmount(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return result_object(ctx, storage::unmount_path(arg_string(ctx, argc, argv, 0)));
}

JSValue js_storage_format(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return result_object(ctx, storage::format_device(arg_string(ctx, argc, argv, 0),
                                                     arg_string(ctx, argc, argv, 1),
                                                     arg_string(ctx, argc, argv, 2),
                                                     arg_string(ctx, argc, argv, 3)));
}

JSValue js_storage_list_dir(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    std::vector<storage::DirEntry> entries;
    bool truncated = false;
    storage::Result result = storage::list_dir(arg_string(ctx, argc, argv, 0), entries, truncated);
    JSValue obj = result_object(ctx, result);
    JSValue array = JS_NewArray(ctx);
    for (size_t i = 0; i < entries.size(); ++i) {
        JSValue entry = JS_NewObject(ctx);
        set_str(ctx, entry, "name", entries[i].name);
        set_bool(ctx, entry, "dir", entries[i].is_dir);
        JS_SetPropertyStr(ctx, entry, "size", JS_NewInt64(ctx, static_cast<int64_t>(entries[i].size)));
        JS_SetPropertyUint32(ctx, array, static_cast<uint32_t>(i), entry);
    }
    JS_SetPropertyStr(ctx, obj, "entries", array);
    set_bool(ctx, obj, "truncated", truncated);
    return obj;
}

JSValue js_storage_read_file(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    std::string data;
    int64_t max_bytes = arg_int(ctx, argc, argv, 1);
    storage::Result result = storage::read_file(arg_string(ctx, argc, argv, 0),
                                                max_bytes > 0 ? static_cast<size_t>(max_bytes) : 0, data);
    JSValue obj = result_object(ctx, result);
    set_str(ctx, obj, "data", data);
    return obj;
}

JSValue js_storage_write_file(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return result_object(ctx, storage::write_file(arg_string(ctx, argc, argv, 0), arg_string(ctx, argc, argv, 1)));
}

JSValue js_storage_make_dir(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return result_object(ctx, storage::make_dir(arg_string(ctx, argc, argv, 0)));
}

JSValue js_storage_remove(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return result_object(ctx, storage::remove_path(arg_string(ctx, argc, argv, 0)));
}

} // namespace linweb
