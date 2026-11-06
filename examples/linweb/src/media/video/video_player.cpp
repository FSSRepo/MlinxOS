#include "media/video/video_player.h"
#include "media/mp4/mp4.h"
#include "media/mp4/yuv.h"
#include "system/process.h"

extern "C" {
#include "libavcodec/avcodec.h"
extern AVCodec ff_h264_decoder;
}
#include "aacdecoder_lib.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <memory>
#include <mutex>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace linweb {
namespace video {

namespace {

using Clock = std::chrono::steady_clock;

constexpr double kAudioLatency = 0.20;     // pipe (16 KiB) + aplay's buffer between a write and the sound
constexpr int kAplayBufferUs = 120000;
constexpr int kPipeBytes = 16384;
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
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char head[12] = {0};
    size_t n = fread(head, 1, sizeof(head), f);
    fclose(f);
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
    if (mp4->fs) {
        if (mp4->fs->file) fclose(mp4->fs->file);
        delete[] mp4->fs->temp_buffer;
        delete mp4->fs;
    }
    delete mp4;
}

class Session {
public:
    ~Session() {
        close_mp4(vmp4);
        close_mp4(amp4);
    }

    Result open(const std::string& file, int sound_card, bool loop_playback) {
        Result r;
        path = file;
        card = sound_card;
        loop = loop_playback;
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
        if (has_audio && process::find_tool("aplay").empty()) {
            has_audio = false;
            error = "aplay is not installed: playing without sound";
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
        // a paused (SIGSTOPped) aplay would leave the audio thread blocked in write(): end it first
        pid_t pid = aplay_pid;
        if (pid > 0) { kill(-pid, SIGCONT); kill(-pid, SIGTERM); }
        if (vthread.joinable()) vthread.join();
        if (athread.joinable()) athread.join();
    }

    // ---- clock ---------------------------------------------------------------------------------------------
    double clock_locked() const {
        if (paused || seeking) return frozen;
        if (source == AUDIO) {
            double played = static_cast<double>(audio_bytes) / (static_cast<double>(audio_rate) * audio_channels * 2.0);
            return std::max(audio_base, audio_base + played - kAudioLatency);
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
        signal_aplay(SIGSTOP);
    }

    void resume() {
        std::lock_guard<std::mutex> lock(clk);
        if (!paused) return;
        if (source == WALL) { base_media = frozen; base_wall = Clock::now(); }
        paused = false;
        signal_aplay(SIGCONT);
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
            audio_bytes = 0;
        } else {
            source = WALL;
            base_media = t;
            base_wall = Clock::now();
        }
        frozen = t;
    }

    void signal_aplay(int sig) {
        pid_t pid = aplay_pid;
        if (pid > 0) kill(-pid, sig);
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
        std::vector<uint8_t> rgb, padded;
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
            yuv420rgb_multi_buffer(f->data[0], f->data[1], f->data[2], rgb.data(), f->width, f->height);
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
    bool start_aplay(int rate, int channels, int& write_fd) {
        int fds[2];
        if (pipe2(fds, O_CLOEXEC) != 0) return false;
#ifdef F_SETPIPE_SZ
        fcntl(fds[1], F_SETPIPE_SZ, kPipeBytes);
#endif
        std::string aplay = process::find_tool("aplay");
        std::vector<std::string> args = {"aplay", "-q", "-t", "raw", "-f", "S16_LE", "-c", std::to_string(channels),
                                         "-r", std::to_string(rate), "--buffer-time=" + std::to_string(kAplayBufferUs)};
        if (card >= 0) { args.push_back("-D"); args.push_back("plughw:" + std::to_string(card) + ",0"); }
        std::vector<char*> argv;
        for (std::string& a : args) argv.push_back(&a[0]);
        argv.push_back(nullptr);

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, fds[0], 0);
        posix_spawnattr_t attr;
        posix_spawnattr_init(&attr);
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&attr, 0);
        pid_t pid = 0;
        int rc = posix_spawn(&pid, aplay.c_str(), &actions, &attr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attr);
        close(fds[0]);
        if (rc != 0) { close(fds[1]); return false; }
        aplay_pid = pid;
        write_fd = fds[1];
        return true;
    }

    void end_aplay(int& write_fd, bool drain) {
        if (write_fd >= 0) { close(write_fd); write_fd = -1; }
        pid_t pid = aplay_pid;
        if (pid <= 0) return;
        aplay_pid = -1;
        kill(-pid, SIGCONT);
        if (!drain) kill(-pid, SIGTERM);
        waitpid(pid, nullptr, 0);
    }

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
        int write_fd = -1;
        int gen = 0;
        int idx = 0;
        bool finished = false;
        int rate = audio_rate, channels = audio_channels;

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
                end_aplay(write_fd, true);   // let aplay play what is buffered
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

            if (write_fd < 0) {
                rate = info->sampleRate;
                channels = info->numChannels;
                if (!start_aplay(rate, channels, write_fd)) {
                    std::lock_guard<std::mutex> lock(clk);
                    audio_failed_locked("cannot start aplay: playing without sound");
                    break;
                }
                std::lock_guard<std::mutex> lock(clk);
                audio_rate = rate;
                audio_channels = channels;
            }
            const char* bytes = reinterpret_cast<const char*>(pcm.data());
            size_t left = static_cast<size_t>(info->frameSize) * info->numChannels * sizeof(INT_PCM);
            bool broken = false;
            while (left > 0 && !quit) {
                ssize_t n = write(write_fd, bytes, left);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) { broken = true; break; }
                bytes += n;
                left -= static_cast<size_t>(n);
                std::lock_guard<std::mutex> lock(clk);
                if (seek_gen == gen) audio_bytes += n;
            }
            if (broken) {
                end_aplay(write_fd, false);
                std::lock_guard<std::mutex> lock(clk);
                audio_failed_locked("the sound device stopped (no sound device?): playing without sound");
                break;
            }
        }
        end_aplay(write_fd, false);
        aacDecoder_Close(dec);
    }

    // ---- state ---------------------------------------------------------------------------------------------
    std::string path;
    int card = -1;
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
    std::atomic<pid_t> aplay_pid{-1};
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
    int64_t audio_bytes = 0;
    int audio_rate = 44100, audio_channels = 2;
    int seek_gen = 0;
    std::string error;

    std::mutex frame_m;
    std::vector<uint8_t> frame;
    int fw = 0, fh = 0;
    uint64_t serial = 0;
};

std::mutex g_lock;
std::shared_ptr<Session> g_session;
std::mutex g_size_lock;
std::map<std::string, std::pair<int, int>> g_sizes;

std::shared_ptr<Session> current() {
    std::lock_guard<std::mutex> lock(g_lock);
    return g_session;
}

Result fail(const std::string& message) {
    Result r;
    r.error = message;
    return r;
}

} // namespace

Result play(const std::string& path, int card, bool loop) {
    signal(SIGPIPE, SIG_IGN);   // a dead aplay must not kill us
    stop();
    auto session = std::make_shared<Session>();
    Result r = session->open(path, card, loop);
    if (!r.ok) return r;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_session = session;
    }
    {
        std::lock_guard<std::mutex> lock(g_size_lock);
        g_sizes[path] = {session->width, session->height};
    }
    session->start();
    return r;
}

Result pause() {
    auto s = current();
    if (!s) return fail("nothing is playing");
    s->pause();
    Result r;
    r.ok = true;
    return r;
}

Result resume() {
    auto s = current();
    if (!s) return fail("nothing is playing");
    s->resume();
    Result r;
    r.ok = true;
    return r;
}

Result stop() {
    std::shared_ptr<Session> s;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        s = g_session;
        g_session.reset();
    }
    if (s) s->stop();
    Result r;
    r.ok = true;
    return r;
}

Result seek(double seconds) {
    auto s = current();
    if (!s) return fail("nothing is playing");
    s->seek(seconds);
    Result r;
    r.ok = true;
    return r;
}

void set_loop(bool loop) {
    auto s = current();
    if (s) s->loop = loop;
}

Status status() {
    Status st;
    auto s = current();
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

bool fetch_frame(uint64_t& serial, const std::function<void(const uint8_t*, int, int)>& use) {
    auto s = current();
    if (!s) return false;
    std::lock_guard<std::mutex> lock(s->frame_m);
    if (s->serial == serial || s->frame.empty()) return false;
    serial = s->serial;
    use(s->frame.data(), s->fw, s->fh);
    return true;
}

bool current_size(int& width, int& height) {
    auto s = current();
    if (!s) return false;
    width = s->width;
    height = s->height;
    return true;
}

std::string current_path() {
    auto s = current();
    return s ? s->path : std::string();
}

void shutdown() {
    stop();
}

} // namespace video
} // namespace linweb
