#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace linweb {
namespace media {

// A sound output for one stream of interleaved signed 16-bit PCM. One thread pushes the samples with write(); the sound
// device (miniaudio: WASAPI, ALSA, PulseAudio, CoreAudio, ...) pulls them from a ring buffer. write() blocks while the
// buffer is full, so a decoder never runs more than `buffer_ms` ahead of the sound. The output is also the clock of the
// playback: played_seconds() counts the audio the device has taken, not the one that was written.
class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    // Opens the default sound device for this format. When paused() is true the device waits for resume().
    bool open(int sample_rate, int channels, int buffer_ms, std::string& error);
    bool is_open() const;
    void close();

    // Queues `frames` frames (frames * channels samples). Blocks until they all fit; false when the output was closed
    // or aborted meanwhile.
    bool write(const int16_t* samples, size_t frames);
    // Wakes a write() that waits for room and makes the following ones fail; close() or open() clear it.
    void abort();

    void pause();
    void resume();
    bool paused() const;

    void set_volume(float volume);   // 0..1, this output only
    float volume() const;

    // Drops what is buffered; played_seconds() starts again from 0.
    void flush();
    // Waits until the buffered audio has been played, at most `timeout_ms`.
    void drain(int timeout_ms);

    // Frames waiting in the buffer to be played.
    size_t buffered_frames() const;

    // Seconds of audio the device has played since open() or the last flush(). Underruns (silence) do not count.
    double played_seconds() const;
    int sample_rate() const;
    int channels() const;

    struct Impl;   // a source of the mixer (audio_output.cpp)

private:
    std::unique_ptr<Impl> impl_;
};

// How loud what the sound device plays is now, for effects that follow the music (the "ring" background of the demo):
// `level` is the overall loudness and `bass` the low frequencies, both 0..1 (0 when nothing plays or everything is paused).
// Cheap enough to call on every screen frame.
void mixer_levels(float& level, float& bass);

} // namespace media
} // namespace linweb
