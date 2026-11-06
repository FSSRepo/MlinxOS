#include "scripting/video_bindings.h"
#include "media/video/video_player.h"
#include "system/audio.h"
#include "system/storage.h"
#include <string>

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

double arg_double(JSContext* ctx, int argc, JSValueConst* argv, int index, double fallback) {
    double value = fallback;
    if (index < argc && JS_ToFloat64(ctx, &value, argv[index]) != 0) return fallback;
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

JSValue result_object(JSContext* ctx, bool ok, const std::string& error) {
    JSValue obj = JS_NewObject(ctx);
    set_bool(ctx, obj, "ok", ok);
    set_str(ctx, obj, "error", error);
    return obj;
}

JSValue result_object(JSContext* ctx, const video::Result& result) {
    return result_object(ctx, result.ok, result.error);
}

} // namespace

// __videoPlay(path, card, loop): the file must be inside the storage mount root, like the music.
JSValue js_video_play(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    std::string resolved;
    storage::Result found = storage::resolve_existing(arg_string(ctx, argc, argv, 0), resolved);
    if (!found.ok) return result_object(ctx, false, found.error);
    audio::stop();   // the MP3 player and a video would fight over the sound card
    return result_object(ctx, video::play(resolved, arg_int(ctx, argc, argv, 1, -1), arg_int(ctx, argc, argv, 2, 0) != 0));
}

JSValue js_video_pause(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return result_object(ctx, video::pause());
}

JSValue js_video_resume(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return result_object(ctx, video::resume());
}

JSValue js_video_stop(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return result_object(ctx, video::stop());
}

JSValue js_video_seek(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return result_object(ctx, video::seek(arg_double(ctx, argc, argv, 0, 0.0)));
}

JSValue js_video_set_loop(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    video::set_loop(arg_int(ctx, argc, argv, 0, 0) != 0);
    return JS_UNDEFINED;
}

JSValue js_video_status(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    video::Status st = video::status();
    JSValue obj = JS_NewObject(ctx);
    set_bool(ctx, obj, "loaded", st.loaded);
    set_bool(ctx, obj, "playing", st.playing);
    set_bool(ctx, obj, "paused", st.paused);
    set_bool(ctx, obj, "ended", st.ended);
    set_bool(ctx, obj, "loop", st.loop);
    set_bool(ctx, obj, "audio", st.audio);
    set_num(ctx, obj, "position", st.position);
    set_num(ctx, obj, "duration", st.duration);
    set_num(ctx, obj, "width", st.width);
    set_num(ctx, obj, "height", st.height);
    set_num(ctx, obj, "fps", st.fps);
    set_num(ctx, obj, "dropped", st.dropped);
    set_str(ctx, obj, "path", st.path);
    set_str(ctx, obj, "error", st.error);
    return obj;
}

} // namespace linweb
