#include "media/video_player.h"
#include "media/audio_output.h"
#include "media/byte_reader.h"
#include "media/mp4/mp4.h"
#include "media/mp4/yuv.h"

extern "C" {
#include "libavcodec/avcodec.h"
extern AVCodec ff_h264_decoder;
}
#include "aacdecoder_lib.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace linweb {
namespace video {

namespace {

using Clock = std::chrono::steady_clock;

constexpr double kAudioLatency = 0.08;     // the sound device plays a little behind the audio it has taken
constexpr int kAudioBufferMs = 400;        // audio held between the decoder and the sound device
constexpr double kDropAfter = 0.15;        // a frame this late is not even converted
constexpr int kAacSamplesPerFrame = 1024;

bool debug_enabled() {
    static const bool on = getenv("LINWEB_VIDEO_DEBUG") != nullptr;
    return on;
}
#define VLOG(...) do { if (debug_enabled()) { fprintf(stderr, "[video] " __VA_ARGS__); fputc(0x0a, stderr); } } while (0)

void sleep_seconds(double seconds) {
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}

bool looks_like_mp4(const std::string& path) {
    std::string error;
    std::unique_ptr<media::ByteReader> reader = media::open_reader(path, error);
    if (!reader) return false;
    unsigned char head[12] = {0};
    const size_t n = reader->read(head, sizeof(head));
    return n == sizeof(head) && memcmp(head + 4, "ftyp", 4) == 0;
}

// One video sample as an Annex B stream (start codes) for the decoder, followed by zeroed padding that is not
// counted in `size`. The MP4 stores length-prefixed NAL units; SPS and PPS are put in front of every IDR picture,
// so decoding can start there. (media-reader's own conversion only recognises the sample kind from the first
// NAL unit of the first sample, which fails when an encoder starts the file with an SEI.)
bool read_annexb(mp4_file* mp4, mp4_track* track, int index, std::vector<uint8_t>& out, int& size, bool& idr) {
    mp4_sample sample;
    mp4_read_video_sample(mp4, track, index, sample, false);
    if (!sample.data) return false;
    const int length_size = std::max(1, std::min(4, track->nalu_length_size));
    static const uint8_t start_code[4] = {0, 0, 0, 1};

    auto nal_length = [&](size_t pos) {
        uint32_t len = 0;
        for (int i = 0; i < length_size; ++i) len = (len << 8) | sample.data[pos + i];
        return len;
    };
    idr = false;
    for (size_t pos = 0; pos + length_size < sample.size;) {
        uint32_t len = nal_length(pos);
        pos += length_size;
        if (len == 0 || pos + len > sample.size) break;
        if ((sample.data[pos] & 0x1f) == 5) idr = true;
        pos += len;
    }

    out.clear();
    out.reserve(sample.size + 256);
    auto append = [&](const uint8_t* data, size_t n) {
        out.insert(out.end(), start_code, start_code + 4);
        out.insert(out.end(), data, data + n);
    };
    if (idr) {
        for (const nal_chunk& sps : track->sequences_nal) append(sps.data, static_cast<size_t>(sps.size));
        for (const nal_chunk& pps : track->pictures_nal) append(pps.data, static_cast<size_t>(pps.size));
    }
    for (size_t pos = 0; pos + length_size < sample.size;) {
        uint32_t len = nal_length(pos);
        pos += length_size;
        if (len == 0 || pos + len > sample.size) break;
        append(sample.data + pos, len);
        pos += len;
    }
    size = static_cast<int>(out.size());
    out.insert(out.end(), AV_INPUT_BUFFER_PADDING_SIZE, 0);
    mp4_free_sample(sample);
    return true;
}

// Last IDR sample at or before `index` (0 when none is found).
int idr_at_or_before(mp4_file* mp4, mp4_track* track, int index) {
    std::vector<uint8_t> scratch;
    for (int i = index; i > 0; --i) {
        int size = 0;
        bool idr = false;
        if (read_annexb(mp4, track, i, scratch, size, idr) && idr) return i;
    }
    return 0;
}

void close_mp4(mp4_file* mp4) {
    if (!mp4) return;
    delete mp4->fs;
    delete mp4;
}

class Session {
public:
    ~Session() {
        close_mp4(vmp4);
        close_mp4(amp4);
    }

    Result open(const std::string& file, bool loop_playback, float start_volume) {
        Result r;
        path = file;
        loop = loop_playback;
        output.set_volume(start_volume);
        if (!looks_like_mp4(path)) { r.error = "not an MP4 file"; return r; }
        vmp4 = mp4_open(path.c_str(), false);
        vtrack = vmp4 ? mp4_get_track(vmp4, VIDEO_TRACK) : nullptr;
        if (!vtrack || vtrack->bs_type == H265_HEVC || vtrack->sample_count <= 0 || vtrack->width <= 0 || vtrack->height <= 0) {
            r.error = "the file has no H.264 video track";
            return r;
        }
        width = vtrack->width;
        height = vtrack->height;
        fps = vtrack->fps > 1.0f && vtrack->fps < 241.0f ? vtrack->fps : 30.0;
        duration = vtrack->time_scale > 0 ? static_cast<double>(vtrack->duration) / vtrack->time_scale : vtrack->sample_count / fps;

        amp4 = mp4_open(path.c_str(), false);
        atrack = amp4 ? mp4_get_track(amp4, AUDIO_TRACK) : nullptr;
        has_audio = atrack && atrack->sample_count > 0 && !atrack->audio_specific_config.empty();
        if (has_audio) {
            audio_rate = atrack->sample_rate > 0 ? atrack->sample_rate : 44100;
            audio_channels = atrack->num_channels > 0 ? atrack->num_channels : 2;
        }
        source = has_audio ? AUDIO : WALL;
        audio_done = !has_audio;
        base_wall = Clock::now();
        r.ok = true;
        return r;
    }

    void start() {
        vthread = std::thread([this] { video_main(); });
        if (has_audio) athread = std::thread([this] { audio_main(); });
    }

    void stop() {
        quit = true;
        output.abort();   // the audio thread may wait for room in the sound buffer
        if (vthread.joinable()) vthread.join();
        if (athread.joinable()) athread.join();
    }

    // ---- clock ---------------------------------------------------------------------------------------------
    double clock_locked() const {
        if (paused || seeking) return frozen;
        if (source == AUDIO) {
            return std::max(audio_base, audio_base + output.played_seconds() - kAudioLatency);
        }
        return base_media + std::chrono::duration<double>(Clock::now() - base_wall).count();
    }

    double position() {
        std::lock_guard<std::mutex> lock(clk);
        return std::min(duration, std::max(0.0, ended ? duration : clock_locked()));
    }

    void switch_to_wall_locked() {
        if (source == WALL) return;
        double t = clock_locked();
        source = WALL;
        base_media = t;
        base_wall = Clock::now();
    }

    void pause() {
        std::lock_guard<std::mutex> lock(clk);
        if (paused) return;
        frozen = clock_locked();
        paused = true;
        output.pause();
    }

    void resume() {
        std::lock_guard<std::mutex> lock(clk);
        if (!paused) return;
        if (source == WALL) { base_media = frozen; base_wall = Clock::now(); }
        paused = false;
        output.resume();
    }

    void seek(double seconds) {
        std::lock_guard<std::mutex> lock(clk);
        seek_locked(std::min(std::max(0.0, seconds), std::max(0.0, duration - 0.2)));
    }

    void seek_locked(double t) {
        seek_target = t;
        ++seek_gen;
        seeking = true;
        VLOG("seek to %.2f s (generation %d)", t, seek_gen);
        ended = false;
        video_done = false;
        audio_done = !has_audio || audio_failed;
        if (has_audio && !audio_failed) {
            source = AUDIO;
            audio_base = t;
            output.flush();
        } else {
            source = WALL;
            base_media = t;
            base_wall = Clock::now();
        }
        frozen = t;
    }

    // Called by each thread when its stream is over; the video loops or ends once both are.
    void part_finished_locked() {
        if (!video_done || !audio_done) return;
        if (loop) seek_locked(0);
        else ended = true;
    }

    // ---- video ---------------------------------------------------------------------------------------------
    bool wait_until(double pts, int gen) {
        for (;;) {
            if (quit) return false;
            double wait;
            {
                std::lock_guard<std::mutex> lock(clk);
                if (seek_gen != gen) return false;
                wait = pts - clock_locked();
            }
            if (wait <= 0.001) return true;
            sleep_seconds(std::min(wait, 0.010));
        }
    }

    void publish(const std::vector<uint8_t>& rgb, int w, int h) {
        std::lock_guard<std::mutex> lock(frame_m);
        frame = rgb;
        fw = w;
        fh = h;
        ++serial;
    }

    void video_main() {
        static std::once_flag registered;
        std::call_once(registered, [] { avcodec_register(&ff_h264_decoder); });
        AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        AVCodecContext* ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
        if (!ctx || avcodec_open2(ctx, codec, nullptr) < 0) {
            std::lock_guard<std::mutex> lock(clk);
            error = "cannot start the H.264 decoder";
            video_done = true;
            part_finished_locked();
            return;
        }
        AVFrame* picture = av_frame_alloc();
        std::vector<uint8_t> rgb, padded, packed;
        const int total = vtrack->sample_count;
        int gen = 0;
        int next = 0;            // sample to feed next
        int64_t shown = 0;       // frames the decoder produced since the last (re)start
        int64_t first = 0;       // sample index the decoder was (re)started on
        double skip_before = 0;  // after a seek, pictures before this time are decoded but not shown
        bool flushed = false;

        auto present = [&](AVFrame* f) {
            double pts = static_cast<double>(first + shown) / fps;
            ++shown;
            if (pts + 0.5 / fps < skip_before) return;
            {
                // the first picture of a new position starts the clock
                std::lock_guard<std::mutex> lock(clk);
                if (seek_gen != gen) return;
                if (seeking) {
                    seeking = false;
                    if (source == WALL) { base_media = frozen; base_wall = Clock::now(); }
                }
            }
            if (!wait_until(pts, gen)) return;
            double late;
            {
                std::lock_guard<std::mutex> lock(clk);
                late = clock_locked() - pts;
            }
            if (late > kDropAfter) { ++dropped; return; }
            rgb.resize(static_cast<size_t>(f->width) * f->height * 3);
            // The converter reads planes of exactly width (and width / 2) bytes per row, but the decoder pads its rows
            // (646 wide has a 672 stride, for example): a picture with such a width needs its rows packed first.
            const int cw = f->width / 2, ch = f->height / 2;
            if (f->linesize[0] == f->width && f->linesize[1] == cw && f->linesize[2] == cw) {
                yuv420rgb_multi_buffer(f->data[0], f->data[1], f->data[2], rgb.data(), f->width, f->height);
            } else {
                const size_t ysize = static_cast<size_t>(f->width) * f->height, csize = static_cast<size_t>(cw) * ch;
                packed.resize(ysize + 2 * csize);
                for (int row = 0; row < f->height; ++row) std::memcpy(&packed[row * f->width], f->data[0] + row * f->linesize[0], f->width);
                for (int row = 0; row < ch; ++row) {
                    std::memcpy(&packed[ysize + row * cw], f->data[1] + row * f->linesize[1], cw);
                    std::memcpy(&packed[ysize + csize + row * cw], f->data[2] + row * f->linesize[2], cw);
                }
                yuv420rgb_multi_buffer(packed.data(), packed.data() + ysize, packed.data() + ysize + csize, rgb.data(), f->width, f->height);
            }
            publish(rgb, f->width, f->height);
        };

        while (!quit) {
            {
                std::lock_guard<std::mutex> lock(clk);
                if (seek_gen != gen) {
                    gen = seek_gen;
                    skip_before = seek_target;
                    next = idr_at_or_before(vmp4, vtrack, std::min(total - 1, static_cast<int>(seek_target * fps)));
                    VLOG("video restarts at sample %d for %.2f s", next, seek_target);
                    first = next;
                    shown = 0;
                    flushed = false;
                    avcodec_flush_buffers(ctx);
                }
            }
            if (next >= total) {
                if (!flushed) {
                    flushed = true;
                    AVPacket empty;
                    av_init_packet(&empty);
                    empty.data = nullptr;
                    empty.size = 0;
                    int got = 0;
                    while (!quit && avcodec_decode_video2(ctx, picture, &got, &empty) >= 0 && got) present(picture);
                    std::lock_guard<std::mutex> lock(clk);
                    VLOG("video stream ended (generation %d of %d, %lld pictures)", gen, seek_gen, static_cast<long long>(shown));
                    if (seek_gen == gen) {
                        if (seeking) { seeking = false; if (source == WALL) { base_media = frozen; base_wall = Clock::now(); } }
                        video_done = true;
                        part_finished_locked();
                    }
                } else {
                    sleep_seconds(0.02);
                }
                continue;
            }

            int size = 0;
            bool idr = false;
            if (!read_annexb(vmp4, vtrack, next, padded, size, idr)) { ++next; continue; }
            AVPacket packet;
            av_init_packet(&packet);
            packet.data = padded.data();
            packet.size = size;
            ++next;
            int got = 0;
            if (avcodec_decode_video2(ctx, picture, &got, &packet) >= 0 && got) present(picture);
        }
        av_frame_free(&picture);
        avcodec_close(ctx);
        av_free(ctx);
    }

    // ---- audio ---------------------------------------------------------------------------------------------
    void audio_failed_locked(const std::string& why) {
        audio_failed = true;
        if (error.empty()) error = why;
        switch_to_wall_locked();
        audio_done = true;
        part_finished_locked();
    }

    void audio_main() {
        HANDLE_AACDECODER dec = aacDecoder_Open(TT_MP4_RAW, 1);
        if (!dec) {
            std::lock_guard<std::mutex> lock(clk);
            audio_failed_locked("cannot start the AAC decoder");
            return;
        }
        UCHAR* asc = atrack->audio_specific_config.data();
        UINT asc_size = static_cast<UINT>(atrack->audio_specific_config.size());
        aacDecoder_ConfigRaw(dec, &asc, &asc_size);

        std::vector<INT_PCM> pcm(8 * 2048);
        bool opened = false;
        int gen = 0;
        int idx = 0;
        bool finished = false;
        const int rate = audio_rate;

        while (!quit) {
            {
                std::lock_guard<std::mutex> lock(clk);
                if (seek_gen != gen) {
                    gen = seek_gen;
                    idx = std::min(atrack->sample_count, static_cast<int>(seek_target * rate / kAacSamplesPerFrame));
                    finished = false;
                    aacDecoder_SetParam(dec, AAC_TPDEC_CLEAR_BUFFER, 1);
                }
            }
            if (finished) { sleep_seconds(0.02); continue; }
            if (idx >= atrack->sample_count) {
                // let the sound that is buffered play (a seek or a stop ends the wait)
                while (!quit && !paused && seek_gen == gen && output.buffered_frames() > 0) sleep_seconds(0.005);
                if (quit) break;
                finished = true;
                std::lock_guard<std::mutex> lock(clk);
                if (seek_gen == gen) {
                    audio_done = true;
                    switch_to_wall_locked();
                    part_finished_locked();
                }
                continue;
            }
            if (paused || seeking) { sleep_seconds(0.01); continue; }

            mp4_sample smpl;
            mp4_read_audio_sample(amp4, atrack, idx++, smpl);
            UCHAR* in = smpl.data;
            UINT valid = static_cast<UINT>(smpl.size);
            bool fed = aacDecoder_Fill(dec, &in, &valid, &valid) == AAC_DEC_OK;
            free(smpl.data);
            if (!fed || aacDecoder_DecodeFrame(dec, pcm.data(), static_cast<INT>(pcm.size()), 0) != AAC_DEC_OK) continue;
            CStreamInfo* info = aacDecoder_GetStreamInfo(dec);
            if (!info || info->sampleRate <= 0 || info->numChannels <= 0) continue;

            if (!opened) {
                std::string why;
                if (!output.open(info->sampleRate, info->numChannels, kAudioBufferMs, why)) {
                    std::lock_guard<std::mutex> lock(clk);
                    audio_failed_locked(why + ": playing without sound");
                    break;
                }
                opened = true;
                if (paused) output.pause();
                std::lock_guard<std::mutex> lock(clk);
                audio_rate = info->sampleRate;
                audio_channels = info->numChannels;
            }
            if (!output.write(reinterpret_cast<const int16_t*>(pcm.data()), static_cast<size_t>(info->frameSize))) {
                if (quit) break;
                std::lock_guard<std::mutex> lock(clk);
                audio_failed_locked("the sound device stopped: playing without sound");
                break;
            }
            if (seek_gen != gen) output.flush();   // what was written while the seek was requested is not wanted
        }
        output.close();
        aacDecoder_Close(dec);
    }

    // ---- state ---------------------------------------------------------------------------------------------
    std::string path;
    std::atomic<bool> loop{false};
    mp4_file* vmp4 = nullptr;
    mp4_file* amp4 = nullptr;
    mp4_track* vtrack = nullptr;
    mp4_track* atrack = nullptr;
    int width = 0, height = 0;
    double fps = 30, duration = 0;
    bool has_audio = false;

    std::atomic<bool> quit{false};
    std::atomic<int> dropped{0};
    media::AudioOutput output;
    std::thread vthread, athread;

    enum Source { AUDIO, WALL };
    mutable std::mutex clk;            // guards everything below up to frame_m
    Source source = WALL;
    std::atomic<bool> paused{false};
    std::atomic<bool> seeking{false};   // after a seek the clock waits for the first picture of the new position
    bool ended = false;
    bool video_done = false;
    bool audio_done = false;
    bool audio_failed = false;
    double base_media = 0, frozen = 0, audio_base = 0, seek_target = 0;
    Clock::time_point base_wall;
    int audio_rate = 44100, audio_channels = 2;
    int seek_gen = 0;
    std::string error;

    std::mutex frame_m;
    std::vector<uint8_t> frame;
    int fw = 0, fh = 0;
    uint64_t serial = 0;
};

std::mutex g_lock;
std::map<int, std::shared_ptr<Session>> g_sessions;
std::mutex g_size_lock;
std::map<std::string, std::pair<int, int>> g_sizes;

std::shared_ptr<Session> find(int id) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_sessions.find(id);
    return it == g_sessions.end() ? nullptr : it->second;
}

Result fail(const std::string& message) {
    Result r;
    r.error = message;
    return r;
}

Result success() {
    Result r;
    r.ok = true;
    return r;
}

} // namespace

Result play(int id, const std::string& path, bool loop) {
    stop(id);
    auto session = std::make_shared<Session>();
    Result r = session->open(path, loop, 1.0f);
    if (!r.ok) return r;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_sessions[id] = session;
    }
    {
        std::lock_guard<std::mutex> lock(g_size_lock);
        g_sizes[path] = {session->width, session->height};
    }
    session->start();
    return r;
}

Result pause(int id) {
    auto s = find(id);
    if (!s) return fail("nothing is playing");
    s->pause();
    return success();
}

Result resume(int id) {
    auto s = find(id);
    if (!s) return fail("nothing is playing");
    s->resume();
    return success();
}

Result stop(int id) {
    std::shared_ptr<Session> s;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        auto it = g_sessions.find(id);
        if (it != g_sessions.end()) { s = it->second; g_sessions.erase(it); }
    }
    if (s) s->stop();
    return success();
}

Result seek(int id, double seconds) {
    auto s = find(id);
    if (!s) return fail("nothing is playing");
    s->seek(seconds);
    return success();
}

void set_volume(int id, float volume) {
    auto s = find(id);
    if (s) s->output.set_volume(std::max(0.0f, std::min(1.0f, volume)));
}

void set_loop(int id, bool loop) {
    auto s = find(id);
    if (s) s->loop = loop;
}

Status status(int id) {
    Status st;
    auto s = find(id);
    if (!s) return st;
    st.loaded = true;
    st.path = s->path;
    st.width = s->width;
    st.height = s->height;
    st.fps = s->fps;
    st.duration = s->duration;
    st.loop = s->loop;
    st.dropped = s->dropped;
    st.position = s->position();
    std::lock_guard<std::mutex> lock(s->clk);
    st.paused = s->paused;
    st.ended = s->ended;
    st.playing = !s->paused && !s->ended;
    st.audio = s->has_audio && !s->audio_failed;
    st.error = s->error;
    return st;
}

bool dimensions(const std::string& path, int& width, int& height) {
    {
        std::lock_guard<std::mutex> lock(g_size_lock);
        auto it = g_sizes.find(path);
        if (it != g_sizes.end()) { width = it->second.first; height = it->second.second; return width > 0; }
    }
    int w = 0, h = 0;
    // the size of a stream is known once it was probed or played: the layout does not wait for the network
    if (media::is_url(path)) return false;
    if (looks_like_mp4(path)) {
        mp4_file* mp4 = mp4_open(path.c_str(), false);
        mp4_track* track = mp4 ? mp4_get_track(mp4, VIDEO_TRACK) : nullptr;
        if (track) { w = track->width; h = track->height; }
        close_mp4(mp4);
    }
    std::lock_guard<std::mutex> lock(g_size_lock);
    g_sizes[path] = {w, h};
    width = w;
    height = h;
    return w > 0;
}

bool probe(const std::string& path, Info& info) {
    info = Info();
    if (!looks_like_mp4(path)) { info.error = "not an MP4 file"; return false; }
    mp4_file* mp4 = mp4_open(path.c_str(), false);
    mp4_track* track = mp4 ? mp4_get_track(mp4, VIDEO_TRACK) : nullptr;
    if (!track || track->bs_type == H265_HEVC || track->sample_count <= 0 || track->width <= 0 || track->height <= 0) {
        close_mp4(mp4);
        info.error = "the file has no H.264 video track";
        return false;
    }
    info.width = track->width;
    info.height = track->height;
    {
        std::lock_guard<std::mutex> lock(g_size_lock);
        g_sizes[path] = {info.width, info.height};
    }
    info.fps = track->fps > 1.0f && track->fps < 241.0f ? track->fps : 30.0;
    info.duration = track->time_scale > 0 ? static_cast<double>(track->duration) / track->time_scale : track->sample_count / info.fps;
    close_mp4(mp4);
    mp4_file* audio = mp4_open(path.c_str(), false);
    mp4_track* atrack = audio ? mp4_get_track(audio, AUDIO_TRACK) : nullptr;
    info.has_audio = atrack && atrack->sample_count > 0 && !atrack->audio_specific_config.empty();
    close_mp4(audio);
    return true;
}

bool fetch_frame(int id, uint64_t& serial, const std::function<void(const uint8_t*, int, int)>& use) {
    auto s = find(id);
    if (!s) return false;
    std::lock_guard<std::mutex> lock(s->frame_m);
    if (s->serial == serial || s->frame.empty()) return false;
    serial = s->serial;
    use(s->frame.data(), s->fw, s->fh);
    return true;
}

bool session_size(int id, int& width, int& height) {
    auto s = find(id);
    if (!s) return false;
    width = s->width;
    height = s->height;
    return true;
}

bool any_playing() {
    std::lock_guard<std::mutex> lock(g_lock);
    for (auto& entry : g_sessions) {
        Session& s = *entry.second;
        if (!s.paused && !s.ended) return true;
    }
    return false;
}

void shutdown() {
    std::map<int, std::shared_ptr<Session>> all;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        all.swap(g_sessions);
    }
    for (auto& entry : all) entry.second->stop();
}

} // namespace video
} // namespace linweb
