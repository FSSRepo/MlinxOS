#pragma once
#include <string>

namespace linweb {
namespace audio {

// Plays MP3 files (MPEG-1 Layer III) through the sound device. Each track decodes in a thread of this process, frame by
// frame, and never gets more than a fraction of a second ahead of the sound.

struct Result {
    bool ok = false;
    std::string error;
};

struct Status {
    bool loaded = false;      // a track is open (playing, paused or ended)
    bool playing = false;     // not paused and not ended
    bool paused = false;
    bool ended = false;
    bool loop = false;
    double position = 0;      // seconds
    double duration = 0;      // estimated from the file size and the bitrate of the first frame (exact for CBR files)
    int sample_rate = 0;
    int channels = 0;
    std::string path;
    std::string error;        // why the playback stopped, empty when it did not
};

struct Info {
    bool ok = false;
    std::string error;
    double duration = 0;
    int sample_rate = 0;
    int channels = 0;
};

// Several tracks can play at once (the sound card mixes them): each one is named by an `id` the caller chooses.
// Opens the file and starts playing it at `start_seconds` as the track `id` (one that already has that id is replaced).
// The path is used as given.
Result play(int id, const std::string& path, double start_seconds = 0.0, bool loop = false);
Result pause(int id);
Result resume(int id);
Result stop(int id);
Result seek(int id, double seconds);
void set_loop(int id, bool loop);
void set_volume(int id, float volume);   // 0..1, this track only
Status status(int id);                   // loaded is false for an id that has no track

// What a file is, without playing it.
Info probe(const std::string& path);

// Stops every track (call before exit).
void shutdown();

} // namespace audio
} // namespace linweb
