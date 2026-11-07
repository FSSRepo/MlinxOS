#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace linweb {
namespace video {

// Plays an MP4 file (H.264 video, AAC audio) with the decoders copied from media-reader.
// One video plays at a time. The picture is read by the renderer (fetch_frame) and shown by <video>
// elements; the sound goes to the sound device, which is also the clock the picture follows. Without an audio track,
// without a sound device the picture follows the system clock instead.

struct Status {
    bool loaded = false;      // a video is open (playing, paused or ended)
    bool playing = false;     // not paused and not ended
    bool paused = false;
    bool ended = false;
    bool loop = false;
    bool audio = false;       // the sound is playing (false: no audio track, or no way to play it)
    double position = 0;      // seconds
    double duration = 0;
    int width = 0;
    int height = 0;
    double fps = 0;
    int dropped = 0;          // frames skipped because the decoder fell behind
    std::string path;
    std::string error;        // why the sound is off, or why the playback stopped
};

struct Result {
    bool ok = false;
    std::string error;
};

// Several videos can play at once (their sounds are mixed): each one is named by an `id` the caller chooses.
// Opens the file and starts playing it as the video `id` (one that already has that id is replaced). The path is used as given.
Result play(int id, const std::string& path, bool loop);
Result pause(int id);
Result resume(int id);
Result stop(int id);
Result seek(int id, double seconds);
void set_loop(int id, bool loop);
void set_volume(int id, float volume);   // 0..1, this video only
Status status(int id);   // loaded is false for an id that has no video

// What an MP4 is, without playing it.
struct Info {
    int width = 0;
    int height = 0;
    double duration = 0;      // seconds
    double fps = 0;
    bool has_audio = false;
    std::string error;
};
bool probe(const std::string& path, Info& info);

// Size of an MP4's picture without playing it (cached by path).
bool dimensions(const std::string& path, int& width, int& height);

// Picture of the video `id` as RGB888 (call from the thread that owns the GL context). When a frame newer than `serial`
// exists, `use(rgb, width, height)` is called with it, `serial` is updated and true is returned.
bool fetch_frame(int id, uint64_t& serial, const std::function<void(const uint8_t*, int, int)>& use);
// Size of the picture of the video `id`, false when it is not loaded.
bool session_size(int id, int& width, int& height);

// True while any video is running (not paused, not ended): the screen has to be drawn again for its next picture.
bool any_playing();

// Stops everything (call before exit).
void shutdown();

} // namespace video
} // namespace linweb
