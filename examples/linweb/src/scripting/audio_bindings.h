#pragma once
#include "quickjs.h"

namespace linweb {

// Sound cards, mixer and MP3 playback for scripts (exposed as linweb.audio).
JSValue js_audio_cards(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_audio_get_volume(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_audio_set_volume(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_audio_set_mute(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_audio_play(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_audio_stop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_audio_pause(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_audio_resume(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_audio_status(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

} // namespace linweb
