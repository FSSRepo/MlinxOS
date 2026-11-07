#include "media/audio_player.h"
#include "media/audio_output.h"
#include "media/byte_reader.h"
#include "media/mp3/mp3_stream.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace linweb {
namespace audio {

namespace {

constexpr int kBufferMs = 400;          // audio held between the decoder and the sound device
constexpr double kLatencyS = 0.06;      // the device plays a little behind the audio it has taken

void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

long file_size(const std::string& path) {
    std::string error;
    std::unique_ptr<media::ByteReader> reader = media::open_reader(path, error);
    return reader ? static_cast<long>(reader->size()) : -1;
}

class Track {
public:
    ~Track() { stop(); }

    Result open(const std::string& file, double start_seconds, bool loop_playback, float volume) {
        Result r;
        path = file;
        loop = loop_playback;
        Mp3Stream probe;
        if (!probe.open(path.c_str())) { r.error = "not a playable MP3 file"; return r; }
        rate = probe.sample_rate();
        channels = probe.channels();
        const long size = file_size(path);
        if (probe.bitrate() > 0 && size > 0) duration = static_cast<double>(size) * 8.0 / probe.bitrate();
        std::string error_text;
        if (!output.open(rate, channels, kBufferMs, error_text)) { r.error = error_text; return r; }
        output.set_volume(volume);
        start = std::max(0.0, std::min(start_seconds, duration > 0 ? duration : start_seconds));
        r.ok = true;
        return r;
    }

    void begin() {
        seek_to = start.load();
        seek_pending = true;
        thread = std::thread([this] { run(); });
    }

    void stop() {
        quit = true;
        output.abort();
        if (thread.joinable()) thread.join();
        output.close();
    }

    void pause() { output.pause(); }
    void resume() { output.resume(); }

    void seek(double seconds) {
        seconds = std::max(0.0, duration > 0 ? std::min(seconds, std::max(0.0, duration - 0.1)) : seconds);
        seek_to = seconds;
        start = seconds;            // position() reads from here at once
        ended = false;
        seek_pending = true;
        output.flush();     // the thread may be waiting for room in the buffer of a paused output: the position must not wait for it
    }

    double position() const {
        if (ended) return duration;
        double p = start + std::max(0.0, output.played_seconds() - kLatencyS);
        return duration > 0 ? std::min(p, duration) : p;
    }

    Status status() const {
        Status s;
        s.loaded = true;
        s.path = path;
        s.loop = loop;
        s.ended = ended;
        s.paused = output.paused();
        s.playing = !s.paused && !ended;
        s.position = position();
        s.duration = duration;
        s.sample_rate = rate;
        s.channels = channels;
        std::lock_guard<std::mutex> guard(error_lock);
        s.error = error;
        return s;
    }

    std::string path;
    std::atomic<bool> loop{false};
    media::AudioOutput output;

private:
    void run() {
        std::unique_ptr<Mp3Stream> stream;
        std::vector<int16_t> pcm(static_cast<size_t>(Mp3Stream::kFrameSamples) * channels);
        bool finished = false;
        while (!quit) {
            if (seek_pending.exchange(false)) {
                // the frames before the new position are decoded and dropped
                stream.reset(new Mp3Stream);
                if (!stream->open(path.c_str())) { fail("cannot read the file"); finished = true; continue; }
                const double target = seek_to;
                for (long skip = static_cast<long>(target * rate / Mp3Stream::kFrameSamples); skip > 0 && !quit; --skip) {
                    if (!stream->read_frame(pcm.data())) break;
                }
                output.flush();
                finished = false;
                ended = false;
            }
            if (finished || !stream) { sleep_ms(10); continue; }
            if (!stream->read_frame(pcm.data())) {
                // the end: let the buffered sound play, then loop or finish (a seek or a stop ends the wait)
                while (!quit && !seek_pending && output.buffered_frames() > 0 && !output.paused()) sleep_ms(5);
                if (quit || seek_pending) continue;
                if (loop) { seek_to = 0; start = 0; seek_pending = true; continue; }
                ended = true;
                finished = true;
                continue;
            }
            if (!output.write(pcm.data(), Mp3Stream::kFrameSamples)) {
                if (quit) break;
                fail("the sound device stopped");
                finished = true;
            }
            if (seek_pending) output.flush();   // what was written while the seek was requested is not wanted
        }
    }

    void fail(const std::string& why) {
        std::lock_guard<std::mutex> guard(error_lock);
        error = why;
        ended = true;
    }

    int rate = 0;
    int channels = 0;
    double duration = 0;
    std::atomic<double> start{0};
    std::atomic<double> seek_to{0};
    std::atomic<bool> seek_pending{false};
    std::atomic<bool> quit{false};
    std::atomic<bool> ended{false};
    mutable std::mutex error_lock;
    std::string error;
    std::thread thread;
};

std::mutex g_lock;
std::map<int, std::shared_ptr<Track>> g_tracks;

std::shared_ptr<Track> find(int id) {
    std::lock_guard<std::mutex> guard(g_lock);
    auto it = g_tracks.find(id);
    return it == g_tracks.end() ? nullptr : it->second;
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

Result play(int id, const std::string& path, double start_seconds, bool loop) {
    stop(id);
    auto track = std::make_shared<Track>();
    Result r = track->open(path, start_seconds, loop, 1.0f);
    if (!r.ok) return r;
    track->begin();
    std::lock_guard<std::mutex> guard(g_lock);
    g_tracks[id] = track;
    return r;
}

Result pause(int id) {
    auto t = find(id);
    if (!t) return fail("nothing is playing");
    t->pause();
    return success();
}

Result resume(int id) {
    auto t = find(id);
    if (!t) return fail("nothing is playing");
    t->resume();
    return success();
}

Result stop(int id) {
    std::shared_ptr<Track> t;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        auto it = g_tracks.find(id);
        if (it != g_tracks.end()) { t = it->second; g_tracks.erase(it); }
    }
    if (t) t->stop();
    return success();
}

Result seek(int id, double seconds) {
    auto t = find(id);
    if (!t) return fail("nothing is playing");
    t->seek(seconds);
    return success();
}

void set_loop(int id, bool loop) {
    auto t = find(id);
    if (t) t->loop = loop;
}

void set_volume(int id, float volume) {
    auto t = find(id);
    if (t) t->output.set_volume(volume);
}

Status status(int id) {
    auto t = find(id);
    return t ? t->status() : Status();
}

Info probe(const std::string& path) {
    Info info;
    Mp3Stream stream;
    if (!stream.open(path.c_str())) { info.error = "not a playable MP3 file"; return info; }
    info.ok = true;
    info.sample_rate = stream.sample_rate();
    info.channels = stream.channels();
    const long size = file_size(path);
    if (stream.bitrate() > 0 && size > 0) info.duration = static_cast<double>(size) * 8.0 / stream.bitrate();
    return info;
}

void shutdown() {
    std::map<int, std::shared_ptr<Track>> all;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        all.swap(g_tracks);
    }
    for (auto& entry : all) entry.second->stop();
}

} // namespace audio
} // namespace linweb
