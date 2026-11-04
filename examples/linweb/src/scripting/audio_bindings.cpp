#include "scripting/audio_bindings.h"
#include "system/audio.h"
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

JSValue js_str(JSContext* ctx, const std::string& value) {
    return JS_NewStringLen(ctx, value.data(), value.size());
}

void set_str(JSContext* ctx, JSValue obj, const char* key, const std::string& value) {
    JS_SetPropertyStr(ctx, obj, key, js_str(ctx, value));
}

void set_bool(JSContext* ctx, JSValue obj, const char* key, bool value) {
    JS_SetPropertyStr(ctx, obj, key, JS_NewBool(ctx, value));
}

void set_num(JSContext* ctx, JSValue obj, const char* key, double value) {
    JS_SetPropertyStr(ctx, obj, key, JS_NewFloat64(ctx, value));
}

JSValue result_object(JSContext* ctx, const audio::Result& result) {
    JSValue obj = JS_NewObject(ctx);
    set_bool(ctx, obj, "ok", result.ok);
    set_str(ctx, obj, "error", result.error);
    return obj;
}

JSValue volume_object(JSContext* ctx, const audio::Volume& v) {
    JSValue obj = JS_NewObject(ctx);
    set_bool(ctx, obj, "ok", v.ok);
    set_str(ctx, obj, "error", v.error);
    set_str(ctx, obj, "control", v.control);
    set_num(ctx, obj, "percent", v.percent);
    set_bool(ctx, obj, "muted", v.muted);
    return obj;
}

} // namespace

JSValue js_audio_cards(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    std::vector<audio::Card> cards = audio::list_cards();
    JSValue array = JS_NewArray(ctx);
    for (size_t i = 0; i < cards.size(); ++i) {
        JSValue obj = JS_NewObject(ctx);
        set_num(ctx, obj, "index", cards[i].index);
        set_str(ctx, obj, "id", cards[i].id);
        set_str(ctx, obj, "name", cards[i].name);
        set_bool(ctx, obj, "playback", cards[i].playback);
        set_bool(ctx, obj, "hdmi", cards[i].hdmi);
        JS_SetPropertyUint32(ctx, array, static_cast<uint32_t>(i), obj);
    }
    return array;
}

JSValue js_audio_get_volume(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return volume_object(ctx, audio::get_volume(arg_int(ctx, argc, argv, 0, 0)));
}

JSValue js_audio_set_volume(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return volume_object(ctx, audio::set_volume(arg_int(ctx, argc, argv, 0, 0), arg_int(ctx, argc, argv, 1, 0)));
}

JSValue js_audio_set_mute(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return result_object(ctx, audio::set_mute(arg_int(ctx, argc, argv, 0, 0), arg_int(ctx, argc, argv, 1, 0) != 0));
}

JSValue js_audio_play(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return result_object(ctx, audio::play_mp3(arg_string(ctx, argc, argv, 0), arg_int(ctx, argc, argv, 1, -1),
                                              arg_int(ctx, argc, argv, 2, 1000)));
}

JSValue js_audio_stop(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return result_object(ctx, audio::stop());
}

JSValue js_audio_pause(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return result_object(ctx, audio::pause());
}

JSValue js_audio_resume(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return result_object(ctx, audio::resume());
}

JSValue js_audio_status(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    audio::PlayerStatus s = audio::status();
    JSValue obj = JS_NewObject(ctx);
    set_bool(ctx, obj, "playing", s.playing);
    set_bool(ctx, obj, "paused", s.paused);
    set_str(ctx, obj, "path", s.path);
    set_num(ctx, obj, "position", s.position);
    set_num(ctx, obj, "duration", s.duration);
    set_num(ctx, obj, "sampleRate", s.sample_rate);
    set_num(ctx, obj, "channels", s.channels);
    set_str(ctx, obj, "error", s.error);
    return obj;
}

} // namespace linweb
