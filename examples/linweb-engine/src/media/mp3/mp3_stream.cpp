#include "mp3_stream.h"
#include "decode.h"
#include "filestream.h"
#include "tables.h"
#include "types.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace linweb {

struct Mp3Stream::Impl {
    std::unique_ptr<file_stream> fs;
    mp3_frame frame;
    std::vector<uint8_t> data;       // frame payload plus padding, the bit reader looks ahead
    OpenMP3::Reservoir* reservoir = nullptr;
    float pcm[2][1152];
    bool checked_info = false;
    bool eof = false;
};

Mp3Stream::Mp3Stream() : impl_(new Impl) {
    impl_->frame = mp3_frame();
    void* mem = std::calloc(1, sizeof(OpenMP3::Reservoir));
    impl_->reservoir = static_cast<OpenMP3::Reservoir*>(mem);
}

Mp3Stream::~Mp3Stream() {
    std::free(impl_->reservoir);
    delete impl_;
}

int Mp3Stream::bitrate() const { return impl_->frame.bitrate; }

bool Mp3Stream::next_pcm() {
    Impl& s = *impl_;
    mp3_frame& frame = s.frame;
    while (!s.eof) {
        if (s.fs->end_of_file(4)) { s.eof = true; break; }
        s.fs->read(4);
        uint32_t info = s.fs->next_uint32();
        if ((info & 0xfff00000) != 0xfff00000) {
            // ID3v2 tags can be concatenated between frames; anything else (an ID3v1
            // "TAG" at the end, garbage) ends the stream.
            s.fs->set(s.fs->count - 4);
            s.fs->read(3);
            if (std::string(s.fs->get_string(3)) == "ID3") {
                s.fs->skip(3);
                s.fs->read(4);
                int tag_size = (s.fs->get() << 21) | (s.fs->get() << 14) | (s.fs->get() << 7) | s.fs->get();
                s.fs->skip(tag_size);
                continue;
            }
            s.eof = true;
            break;
        }

        int layer = (info & 0x00060000) >> 17;
        int protection = (info & 0x00010000) >> 16;
        int bitrate_index = (info & 0x0000f000) >> 12;
        int padding = (info & 0x00000200) >> 9;
        int mode = (info & 0x000000c0) >> 6;
        frame.m_sr_index = (info & 0x00000c00) >> 10;
        frame.mode_extension = (info & 0x00000030) >> 4;
        if (layer != 1 || bitrate_index == 0 || bitrate_index == 15 || frame.m_sr_index == 3) {
            s.eof = true;
            break;
        }
        if (!protection) s.fs->skip(2); // CRC

        frame.bitrate = bitrates_layer3_list[bitrate_index];
        frame.samplerate = samplerates_layer3_list[frame.m_sr_index];
        frame.mode = mode == 3 ? MODE_MONO : MODE_STEREO;
        if (frame.bitrate <= 0 || frame.samplerate <= 0) { s.eof = true; break; }

        int total = (144 * frame.bitrate) / frame.samplerate + padding - (protection ? 0 : 2);
        int payload = total - 4;
        int side_info = frame.mode == MODE_MONO ? 17 : 32;
        if (payload <= side_info || payload > 4096 || s.fs->end_of_file(payload - 1)) { s.eof = true; break; }
        s.data.assign(static_cast<size_t>(payload) + 8, 0);
        s.fs->read(s.data.data(), payload);
        frame.data = s.data.data();
        frame.size = payload;

        // The Xing/Info header lives in the first frame and carries no audio.
        if (!s.checked_info) {
            s.checked_info = true;
            if (!std::memcmp(frame.data + side_info, "Info", 4) || !std::memcmp(frame.data + side_info, "Xing", 4)) continue;
        }

        if (mp3_decode(frame, s.pcm, *s.reservoir)) return true;
    }
    return false;
}

static void to_int16(const float pcm[2][1152], int channels, int16_t* out) {
    for (int i = 0; i < 1152; ++i) {
        for (int ch = 0; ch < channels; ++ch) {
            int x = static_cast<int>(pcm[ch][i] * 32767.f);
            out[i * channels + ch] = static_cast<int16_t>(x > 32767 ? 32767 : (x < -32767 ? -32767 : x));
        }
    }
}

bool Mp3Stream::open(const char* path) {
    Impl& s = *impl_;
    s.fs.reset(new file_stream(path));
    if (!s.fs->file) return false;
    if (!next_pcm()) return false;
    sample_rate_ = s.frame.samplerate;
    channels_ = s.frame.mode == MODE_MONO ? 1 : 2;
    first_.resize(static_cast<size_t>(kFrameSamples) * channels_);
    to_int16(s.pcm, channels_, first_.data());
    have_first_ = true;
    return true;
}

bool Mp3Stream::read_frame(int16_t* out) {
    if (have_first_) {
        std::memcpy(out, first_.data(), first_.size() * sizeof(int16_t));
        have_first_ = false;
        return true;
    }
    if (!next_pcm()) return false;
    to_int16(impl_->pcm, channels_, out);
    return true;
}

} // namespace linweb
