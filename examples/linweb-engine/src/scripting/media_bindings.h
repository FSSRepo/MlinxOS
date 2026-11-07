#pragma once
#include "scripting/js_engine.h"

namespace linweb {

// <audio> and <video> elements and `new Audio()`: the HTMLMediaElement API of a browser (src, play(), pause(), currentTime,
// duration, volume, muted, loop, autoplay, readyState, error, canPlayType(), the events and the on<event> handlers),
// on top of the MP3 and MP4 players of media/. Elements play at the same time and their sounds are mixed.
// Install it after the event bindings.
void install_media_bindings(JSEngine& js);

// Stops the sound and the picture (call before exit).
void shutdown_media();

} // namespace linweb
