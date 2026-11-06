#pragma once
#include "quickjs.h"

namespace linweb {

// Video playback for scripts (exposed as linweb.video); <video> elements show the picture.
JSValue js_video_play(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_video_pause(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_video_resume(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_video_stop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_video_seek(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_video_set_loop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
JSValue js_video_status(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

} // namespace linweb
