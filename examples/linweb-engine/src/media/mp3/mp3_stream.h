#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>

// Frame-by-frame MP3 (MPEG-1 Layer III) decoder on top of OpenMP3. Unlike decoding the
// whole file up front, each call to read_frame() produces 1152 samples per channel, so a
// player can decode while it plays and keep only a small margin of audio ahead.
namespace linweb {

class Mp3Stream {
public:
    Mp3Stream();
    ~Mp3Stream();
    Mp3Stream(const Mp3Stream&) = delete;
    Mp3Stream& operator=(const Mp3Stream&) = delete;

    // Reads the first audio frame to learn the sample rate and channel count.
    bool open(const char* path);

    int sample_rate() const { return sample_rate_; }
    int channels() const { return channels_; }
    int bitrate() const;   // bits per second of the first frame (0 before open())
    static const int kFrameSamples = 1152;

    // Decodes the next frame into `out` (kFrameSamples * channels() interleaved int16
    // samples). Returns false at the end of the stream or on corrupt data.
    bool read_frame(int16_t* out);

private:
    bool next_pcm();   // fills pcm_ with the next decoded frame

    struct Impl;
    Impl* impl_;
    int sample_rate_ = 0;
    int channels_ = 0;
    std::vector<int16_t> first_;   // frame decoded by open(), handed out by the first read_frame()
    bool have_first_ = false;
};

} // namespace linweb
