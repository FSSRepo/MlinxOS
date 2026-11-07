#include "scripting/media_bindings.h"
#include "media/audio_player.h"
#include "media/byte_reader.h"
#include "media/video_player.h"
#include "app/app_state.h"
#include "renderer/renderer.h"
#include "utils/file_utils.h"
#include <cctype>
#include <cstdint>
#include <filesystem>
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

bool is_video(JSContext* ctx, int argc, JSValueConst* argv) {
    return arg_string(ctx, argc, argv, 0) == "video";
}

void set_str(JSContext* ctx, JSValue obj, const char* key, const std::string& value) {
    JS_SetPropertyStr(ctx, obj, key, JS_NewStringLen(ctx, value.data(), value.size()));
}
void set_bool(JSContext* ctx, JSValue obj, const char* key, bool value) { JS_SetPropertyStr(ctx, obj, key, JS_NewBool(ctx, value)); }
void set_num(JSContext* ctx, JSValue obj, const char* key, double value) { JS_SetPropertyStr(ctx, obj, key, JS_NewFloat64(ctx, value)); }

JSValue result(JSContext* ctx, bool ok, const std::string& error) {
    JSValue obj = JS_NewObject(ctx);
    set_bool(ctx, obj, "ok", ok);
    set_str(ctx, obj, "error", error);
    return obj;
}

// "%20" and the like back to the characters of a file name; the query and the fragment of an URL are not part of it
std::string url_to_path(const std::string& url) {
    std::string path = url.substr(0, url.find_first_of("?#"));
    std::string out;
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '%' && i + 2 < path.size() + 0 && std::isxdigit(static_cast<unsigned char>(path[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(path[i + 2]))) {
            out += static_cast<char>(std::stoi(path.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += path[i];
        }
    }
    return out;
}

// __mediaResolve(src): the file a src points to, as an absolute path (relative sources are looked up from the page)
JSValue js_media_resolve(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    std::string src = arg_string(ctx, argc, argv, 0);
    if (media::is_url(src)) return JS_NewStringLen(ctx, src.data(), src.size());   // streamed: the address is kept as it is
    if (src.compare(0, 7, "file://") == 0) src = src.substr(src.size() > 8 && src[8] == ':' ? 8 : 7);
    src = url_to_path(src);
    std::string resolved = resolve_image_src(src);
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(std::filesystem::u8path(resolved), ec);
    return JS_NewString(ctx, (ec ? resolved : absolute.lexically_normal().u8string()).c_str());
}

// __mediaProbe(kind, path): { ok, error, duration, width, height, audio }
JSValue js_media_probe(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const std::string path = arg_string(ctx, argc, argv, 1);
    JSValue obj = JS_NewObject(ctx);
    if (is_video(ctx, argc, argv)) {
        video::Info info;
        const bool ok = video::probe(path, info);
        if (ok) g_dom_dirty = true;   // a <video> takes the size of its picture: the layout has to run again
        set_bool(ctx, obj, "ok", ok);
        set_str(ctx, obj, "error", info.error);
        set_num(ctx, obj, "duration", info.duration);
        set_num(ctx, obj, "width", info.width);
        set_num(ctx, obj, "height", info.height);
        set_bool(ctx, obj, "audio", info.has_audio);
    } else {
        audio::Info info = audio::probe(path);
        set_bool(ctx, obj, "ok", info.ok);
        set_str(ctx, obj, "error", info.error);
        set_num(ctx, obj, "duration", info.duration);
        set_num(ctx, obj, "width", 0);
        set_num(ctx, obj, "height", 0);
        set_bool(ctx, obj, "audio", info.ok);
    }
    return obj;
}

// The natives take the kind of the element ("audio" or "video") and the number of its session; the sounds of all the
// sessions that play are mixed.

// __mediaPlay(kind, id, path, startSeconds, loop): starts the file as the session `id` (a session with that id is replaced)
JSValue js_media_play(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const int id = arg_int(ctx, argc, argv, 1, 0);
    const std::string path = arg_string(ctx, argc, argv, 2);
    const double start = arg_double(ctx, argc, argv, 3, 0.0);
    const bool loop = arg_double(ctx, argc, argv, 4, 0.0) != 0.0;
    if (is_video(ctx, argc, argv)) {
        video::Result r = video::play(id, path, loop);
        if (r.ok && start > 0.0) video::seek(id, start);
        return result(ctx, r.ok, r.error);
    }
    audio::Result r = audio::play(id, path, start, loop);
    return result(ctx, r.ok, r.error);
}

JSValue js_media_pause(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const int id = arg_int(ctx, argc, argv, 1, 0);
    if (is_video(ctx, argc, argv)) { video::Result r = video::pause(id); return result(ctx, r.ok, r.error); }
    audio::Result r = audio::pause(id);
    return result(ctx, r.ok, r.error);
}

JSValue js_media_resume(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const int id = arg_int(ctx, argc, argv, 1, 0);
    if (is_video(ctx, argc, argv)) { video::Result r = video::resume(id); return result(ctx, r.ok, r.error); }
    audio::Result r = audio::resume(id);
    return result(ctx, r.ok, r.error);
}

JSValue js_media_stop(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const int id = arg_int(ctx, argc, argv, 1, 0);
    if (is_video(ctx, argc, argv)) video::stop(id); else audio::stop(id);
    return result(ctx, true, "");
}

JSValue js_media_seek(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const int id = arg_int(ctx, argc, argv, 1, 0);
    const double seconds = arg_double(ctx, argc, argv, 2, 0.0);
    if (is_video(ctx, argc, argv)) { video::Result r = video::seek(id, seconds); return result(ctx, r.ok, r.error); }
    audio::Result r = audio::seek(id, seconds);
    return result(ctx, r.ok, r.error);
}

JSValue js_media_set_loop(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const int id = arg_int(ctx, argc, argv, 1, 0);
    const bool loop = arg_double(ctx, argc, argv, 2, 0.0) != 0.0;
    if (is_video(ctx, argc, argv)) video::set_loop(id, loop); else audio::set_loop(id, loop);
    return JS_UNDEFINED;
}

JSValue js_media_set_volume(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const int id = arg_int(ctx, argc, argv, 1, 0);
    const float volume = static_cast<float>(arg_double(ctx, argc, argv, 2, 1.0));
    if (is_video(ctx, argc, argv)) video::set_volume(id, volume); else audio::set_volume(id, volume);
    return JS_UNDEFINED;
}

// __mediaStatus(kind, id): { loaded, playing, paused, ended, position, duration, error }
JSValue js_media_status(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const int id = arg_int(ctx, argc, argv, 1, 0);
    JSValue obj = JS_NewObject(ctx);
    if (is_video(ctx, argc, argv)) {
        video::Status s = video::status(id);
        set_bool(ctx, obj, "loaded", s.loaded);
        set_bool(ctx, obj, "playing", s.playing);
        set_bool(ctx, obj, "paused", s.paused);
        set_bool(ctx, obj, "ended", s.ended);
        set_num(ctx, obj, "position", s.position);
        set_num(ctx, obj, "duration", s.duration);
        set_str(ctx, obj, "error", s.error);
    } else {
        audio::Status s = audio::status(id);
        set_bool(ctx, obj, "loaded", s.loaded);
        set_bool(ctx, obj, "playing", s.playing);
        set_bool(ctx, obj, "paused", s.paused);
        set_bool(ctx, obj, "ended", s.ended);
        set_num(ctx, obj, "position", s.position);
        set_num(ctx, obj, "duration", s.duration);
        set_str(ctx, obj, "error", s.error);
    }
    return obj;
}

// __mediaBindVideo(address, id): the <video> element at that address shows the picture of the video session `id`;
// 0 takes the element out
JSValue js_media_bind_video(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int64_t address = 0;
    if (argc > 0) JS_ToInt64Ext(ctx, &address, argv[0]);
    Renderer::bind_video(reinterpret_cast<const void*>(static_cast<intptr_t>(address)), arg_int(ctx, argc, argv, 1, 0));
    return JS_UNDEFINED;
}

} // namespace

void shutdown_media() {
    audio::shutdown();
    video::shutdown();
}

void install_media_bindings(JSEngine& js) {
    js.add_function("__mediaResolve", js_media_resolve);
    js.add_function("__mediaProbe", js_media_probe);
    js.add_function("__mediaPlay", js_media_play);
    js.add_function("__mediaPause", js_media_pause);
    js.add_function("__mediaResume", js_media_resume);
    js.add_function("__mediaStop", js_media_stop);
    js.add_function("__mediaSeek", js_media_seek);
    js.add_function("__mediaSetLoop", js_media_set_loop);
    js.add_function("__mediaSetVolume", js_media_set_volume);
    js.add_function("__mediaStatus", js_media_status);
    js.add_function("__mediaBindVideo", js_media_bind_video);

    js.execute(R"JS(
// ---- HTMLMediaElement for <audio> and <video> ---------------------------------------------------------------------
const __media = (() => {
  const TICK_MS = 100;            // how often the players that sound are looked at
  const TIMEUPDATE_MS = 250;      // timeupdate events while an element plays
  const NETWORK_EMPTY = 0, NETWORK_IDLE = 1, NETWORK_NO_SOURCE = 3;
  const HAVE_NOTHING = 0, HAVE_ENOUGH_DATA = 4;
  const states = new Map();       // element address -> state
  const playing = new Set();      // the states that sound now; their sounds are mixed
  let nextId = 1;                 // number of the player session of each element
  let timer = null;

  const EVENTS = ['loadstart', 'durationchange', 'loadedmetadata', 'loadeddata', 'canplay', 'canplaythrough', 'play', 'playing',
                  'pause', 'seeking', 'seeked', 'timeupdate', 'ended', 'volumechange', 'error', 'emptied'];

  const tagOf = (node) => String(node.tagName || '').toLowerCase();
  const attr = (s, name) => __getAttribute(s.addr, name);
  const hasAttr = (s, name) => __hasAttribute(s.addr, name);

  function stateOf(node) {
    const key = String(node._addr);
    let s = states.get(key);
    if (s) return s;
    s = {
      addr: Number(node._addr), id: nextId++, tag: tagOf(node), handlers: {},
      src: '', currentSrc: '', path: '', ready: false, loaded: false, live: false, token: 0,
      paused: true, ended: false, position: 0, duration: NaN, readyState: HAVE_NOTHING, networkState: NETWORK_EMPTY,
      error: null, videoWidth: 0, videoHeight: 0, volume: 1, muted: hasAttr({ addr: Number(node._addr) }, 'muted'), seeking: false,
      lastTimeUpdate: 0
    };
    states.set(key, s);
    // an element that names a source starts loading it, like in a browser
    if (sourceOf(s)) load(s);
    return s;
  }

  const wrapOf = (s) => document._wrap({ _addr: s.addr, tagName: s.tag });

  function fire(s, type) {
    const handler = s.handlers[type];
    if (typeof handler === 'function') {
      try { handler.call(wrapOf(s), { type: type, target: wrapOf(s) }); } catch (e) { console.error(e); }
    }
    __deliver(s.addr, type, {});
  }
  const later = (s, type) => setTimeout(() => fire(s, type), 0);

  function canPlayType(tag, type) {
    const m = String(type || '').toLowerCase().replace(/\s+/g, '').match(/^([a-z]+\/[a-z0-9.+-]+)(?:;codecs=["']?([^"']*)["']?)?/);
    if (!m) return '';
    const mime = m[1];
    const codecs = m[2] ? m[2].split(',') : [];
    if (mime === 'audio/mpeg' || mime === 'audio/mp3') return tag === 'audio' ? 'probably' : '';
    if (mime === 'video/mp4' && tag === 'video') {
      if (!codecs.length) return 'maybe';
      return codecs.every(c => /^avc1\./.test(c) || /^mp4a\.40\.[25]$/.test(c)) ? 'probably' : '';
    }
    return '';
  }

  // the first usable source: the src attribute, or the first <source> child that this element can play
  function sourceOf(s) {
    const own = attr(s, 'src');
    if (own) return own;
    for (const child of __getElementChildren(s.addr)) {
      if (tagOf(child) !== 'source') continue;
      const src = __getAttribute(child._addr, 'src');
      if (!src) continue;
      const type = __getAttribute(child._addr, 'type');
      if (type && !canPlayType(s.tag, type)) continue;
      return src;
    }
    return '';
  }

  function stopTimer() { if (timer !== null) { clearInterval(timer); timer = null; } }
  function startTimer() { if (timer === null) timer = setInterval(tick, TICK_MS); }

  // the session of the element in the players is gone (the source changed)
  function dropSession(s) {
    if (!s.live) return;
    __mediaStop(s.tag, s.id);
    if (s.tag === 'video') __mediaBindVideo(s.addr, 0);
    s.live = false;
    playing.delete(s);
  }

  function load(s) {
    dropSession(s);
    s.token++;
    s.loaded = true;
    s.ready = false;
    s.paused = true;
    s.ended = false;
    s.position = 0;
    s.duration = NaN;
    s.readyState = HAVE_NOTHING;
    s.error = null;
    s.videoWidth = s.videoHeight = 0;
    s.src = sourceOf(s);
    s.currentSrc = '';
    if (!s.src) { s.networkState = NETWORK_EMPTY; return; }
    const path = __mediaResolve(s.src);
    const info = __mediaProbe(s.tag, path);
    const token = s.token;
    if (!info.ok) {
      s.error = { code: 4, message: info.error || 'unsupported source' };
      s.networkState = NETWORK_NO_SOURCE;
      setTimeout(() => { if (token === s.token) { fire(s, 'loadstart'); fire(s, 'error'); } }, 0);
      return;
    }
    s.path = path;
    s.currentSrc = s.src;
    s.duration = info.duration > 0 ? info.duration : NaN;
    s.videoWidth = info.width || 0;
    s.videoHeight = info.height || 0;
    s.ready = true;
    s.readyState = HAVE_ENOUGH_DATA;
    s.networkState = NETWORK_IDLE;
    setTimeout(() => {
      if (token !== s.token) return;
      ['loadstart', 'durationchange', 'loadedmetadata', 'loadeddata', 'canplay', 'canplaythrough'].forEach(t => fire(s, t));
      if (hasAttr(s, 'autoplay') && s.paused && token === s.token) play(s).catch(() => {});
    }, 0);
  }

  function currentTime(s) {
    if (s.live && !s.ended) {
      const st = __mediaStatus(s.tag, s.id);
      if (st.loaded) return st.position;
    }
    return s.position;
  }

  const rejection = (name, message) => Object.assign(new Error(message), { name: name });

  function applyVolume(s) { if (s.live) __mediaSetVolume(s.tag, s.id, s.muted ? 0 : s.volume); }

  function play(s) {
    return new Promise((resolve, reject) => {
      if (!s.loaded) load(s);
      if (!s.ready) { reject(rejection('NotSupportedError', (s.error && s.error.message) || 'no playable source')); return; }
      if (s.live && !s.ended) {
        // already started: playing it again resumes it when it was paused
        if (s.paused) {
          __mediaResume(s.tag, s.id);
          s.paused = false;
          playing.add(s);
          s.lastTimeUpdate = Date.now();
          startTimer();
          later(s, 'play');
          later(s, 'playing');
        }
        resolve();
        return;
      }
      const from = s.ended ? 0 : s.position;
      const started = __mediaPlay(s.tag, s.id, s.path, from, hasAttr(s, 'loop') ? 1 : 0);
      if (!started.ok) {
        s.error = { code: 4, message: started.error };
        later(s, 'error');
        reject(rejection('NotSupportedError', started.error));
        return;
      }
      s.live = true;
      if (s.tag === 'video') __mediaBindVideo(s.addr, s.id);
      applyVolume(s);
      const wasPaused = s.paused;
      s.paused = false;
      s.ended = false;
      s.lastTimeUpdate = Date.now();
      playing.add(s);
      startTimer();
      if (wasPaused) later(s, 'play');
      later(s, 'playing');
      resolve();
    });
  }

  function pause(s) {
    if (s.paused) return;
    if (s.live) {
      s.position = currentTime(s);
      __mediaPause(s.tag, s.id);
    }
    playing.delete(s);
    s.paused = true;
    later(s, 'pause');
  }

  function seek(s, t) {
    t = Number(t);
    if (!isFinite(t)) throw new TypeError("Failed to set the 'currentTime' property: the value is not finite.");
    t = Math.max(0, isFinite(s.duration) ? Math.min(t, s.duration) : t);
    if (!s.ready) { s.position = t; return; }
    s.seeking = true;
    later(s, 'seeking');
    if (s.live) {
      __mediaSeek(s.tag, s.id, t);
      if (s.paused) __mediaPause(s.tag, s.id);   // a seek makes an ended player run again: it stays as the page left it
    }
    s.position = t;
    s.ended = false;
    setTimeout(() => { s.seeking = false; fire(s, 'seeked'); fire(s, 'timeupdate'); }, 0);
  }

  function finish(s) {
    s.position = isFinite(s.duration) ? s.duration : s.position;
    s.ended = true;
    s.paused = true;
    playing.delete(s);
    fire(s, 'timeupdate');
    fire(s, 'pause');
    fire(s, 'ended');
  }

  function tick() {
    if (playing.size === 0) { stopTimer(); return; }
    const now = Date.now();
    for (const s of Array.from(playing)) {
      const st = __mediaStatus(s.tag, s.id);
      if (s.tag === 'audio' && st.error) {
        s.error = { code: 3, message: st.error };
        s.paused = true;
        playing.delete(s);
        fire(s, 'error');
        fire(s, 'pause');
        continue;
      }
      if (st.ended || !st.loaded) { finish(s); continue; }
      if (!s.paused && now - s.lastTimeUpdate >= TIMEUPDATE_MS) { s.lastTimeUpdate = now; fire(s, 'timeupdate'); }
    }
  }

  function range(s) {
    const end = isFinite(s.duration) ? s.duration : 0;
    return { length: s.ready ? 1 : 0, start: () => 0, end: () => end };
  }

  const setBoolAttr = (s, name, on) => { if (on) __setAttribute(s.addr, name, ''); else __removeAttribute(s.addr, name); };

  document._wrappers.push((node) => {
    const tag = tagOf(node);
    if (tag !== 'audio' && tag !== 'video') return {};
    const s = stateOf(node);
    const el = {
      HAVE_NOTHING: 0, HAVE_METADATA: 1, HAVE_CURRENT_DATA: 2, HAVE_FUTURE_DATA: 3, HAVE_ENOUGH_DATA: 4,
      NETWORK_EMPTY: 0, NETWORK_IDLE: 1, NETWORK_LOADING: 2, NETWORK_NO_SOURCE: 3,
      get src() { return attr(s, 'src') || ''; },
      set src(v) { __setAttribute(s.addr, 'src', String(v)); load(s); },
      get currentSrc() { return s.currentSrc; },
      get currentTime() { return currentTime(s); },
      set currentTime(v) { seek(s, v); },
      get duration() { return s.duration; },
      get paused() { return s.paused; },
      get ended() { return s.ended; },
      get seeking() { return s.seeking; },
      get readyState() { return s.readyState; },
      get networkState() { return s.networkState; },
      get error() { return s.error; },
      get volume() { return s.volume; },
      set volume(v) {
        v = Number(v);
        if (!(v >= 0 && v <= 1)) throw Object.assign(new Error('The volume provided is outside the range [0, 1].'), { name: 'IndexSizeError' });
        if (v === s.volume) return;
        s.volume = v;
        applyVolume(s);
        later(s, 'volumechange');
      },
      get muted() { return s.muted; },
      set muted(v) {
        v = !!v;
        if (v === s.muted) return;
        s.muted = v;
        applyVolume(s);
        later(s, 'volumechange');
      },
      get loop() { return hasAttr(s, 'loop'); },
      set loop(v) { setBoolAttr(s, 'loop', !!v); if (s.live) __mediaSetLoop(s.tag, s.id, v ? 1 : 0); },
      get autoplay() { return hasAttr(s, 'autoplay'); },
      set autoplay(v) { setBoolAttr(s, 'autoplay', !!v); },
      get controls() { return hasAttr(s, 'controls'); },
      set controls(v) { setBoolAttr(s, 'controls', !!v); },
      get preload() { return attr(s, 'preload') || 'auto'; },
      set preload(v) { __setAttribute(s.addr, 'preload', String(v)); },
      get defaultMuted() { return hasAttr(s, 'muted'); },
      get playbackRate() { return 1; },
      set playbackRate(v) {},
      get defaultPlaybackRate() { return 1; },
      get buffered() { return range(s); },
      get seekable() { return range(s); },
      get played() { return range(s); },
      get videoWidth() { return s.videoWidth; },
      get videoHeight() { return s.videoHeight; },
      play: () => play(s),
      pause: () => pause(s),
      load: () => load(s),
      canPlayType: (type) => canPlayType(s.tag, type)
    };
    if (tag === 'video') {
      Object.defineProperty(el, 'poster', { get() { return attr(s, 'poster') || ''; }, set(v) { __setAttribute(s.addr, 'poster', String(v)); }, enumerable: true });
    }
    EVENTS.forEach((type) => {
      Object.defineProperty(el, 'on' + type, {
        get() { return s.handlers[type] || null; },
        set(fn) { s.handlers[type] = typeof fn === 'function' ? fn : null; },
        enumerable: true
      });
    });
    return el;
  });

  // the elements written in the page start loading (and playing, with autoplay) once the page is ready
  document.addEventListener('DOMContentLoaded', () => {
    document.querySelectorAll('video').forEach(() => {});
    document.querySelectorAll('audio').forEach(() => {});
  });

  return { states: states };
})();

// new Audio(src) is an <audio> element that is not in the page
function Audio(src) {
  const element = document.createElement('audio');
  if (src !== undefined) element.src = src;
  return element;
}
const __is_media = (tag) => (object) => !!object && String(object.tagName || '').toLowerCase() === tag;
function HTMLMediaElement() {}
function HTMLAudioElement() {}
function HTMLVideoElement() {}
Object.defineProperty(HTMLMediaElement, Symbol.hasInstance, { value: (o) => __is_media('audio')(o) || __is_media('video')(o) });
Object.defineProperty(HTMLAudioElement, Symbol.hasInstance, { value: __is_media('audio') });
Object.defineProperty(HTMLVideoElement, Symbol.hasInstance, { value: __is_media('video') });
Object.assign(__window, { Audio: Audio, HTMLMediaElement: HTMLMediaElement, HTMLAudioElement: HTMLAudioElement, HTMLVideoElement: HTMLVideoElement });
)JS");
}

} // namespace linweb
