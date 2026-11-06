#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace linweb {
namespace video {

// Plays an MP4 file (H.264 video, AAC audio) with the decoders copied from media-reader.
// One video plays at a time. The picture is read by the renderer (fetch_frame) and shown by <video>
// elements; the sound goes to aplay, which is also the clock the picture follows. Without an audio track,
// without aplay or without a sound card the picture follows the system clock instead.

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

// Opens the file and starts playing. card < 0 uses ALSA's default device. The path is used as given.
Result play(const std::string& path, int card, bool loop);
Result pause();
Result resume();
Result stop();
Result seek(double seconds);
void set_loop(bool loop);
Status status();

// Size of an MP4's picture without playing it (cached by path).
bool dimensions(const std::string& path, int& width, int& height);

// Picture of the current video as RGB888 (call from the thread that owns the GL context). When a frame newer
// than `serial` exists, `use(rgb, width, height)` is called with it, `serial` is updated and true is returned.
bool fetch_frame(uint64_t& serial, const std::function<void(const uint8_t*, int, int)>& use);
// Size of the picture being played, false when nothing is loaded.
bool current_size(int& width, int& height);
// Path of the file being played, empty when nothing is loaded.
std::string current_path();

// Stops everything (call before exit).
void shutdown();

} // namespace video
} // namespace linweb
