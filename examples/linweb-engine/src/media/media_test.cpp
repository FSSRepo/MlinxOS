// Command line check of the media players, without a window:
//
//   linweb-media-test probe FILE            what the file is (MP3 or MP4)
//   linweb-media-test mp3 FILE [SECONDS]    plays an MP3, pauses, resumes and seeks, printing the position
//   linweb-media-test mp4 FILE [SECONDS]    plays an MP4 and prints the position, the pictures decoded and the late ones
//   linweb-media-test mix MP3 MP3 MP4       plays the three at once (their sounds are mixed) and pauses them one by one
#include "media/audio_player.h"
#include "media/video_player.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

using namespace linweb;

namespace {

void wait(double seconds) {
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}

int probe(const std::string& file) {
    audio::Info a = audio::probe(file);
    if (a.ok) {
        std::printf("MP3: %.1f s, %d Hz, %d channels\n", a.duration, a.sample_rate, a.channels);
        return 0;
    }
    video::Info v;
    if (video::probe(file, v)) {
        std::printf("MP4: %dx%d, %.2f fps, %.1f s, audio %s\n", v.width, v.height, v.fps, v.duration, v.has_audio ? "yes" : "no");
        return 0;
    }
    std::printf("not supported (%s / %s)\n", a.error.c_str(), v.error.c_str());
    return 1;
}

int play_mp3(const std::string& file, double seconds) {
    audio::Result r = audio::play(1, file);
    if (!r.ok) { std::printf("play failed: %s\n", r.error.c_str()); return 1; }
    for (double t = 0; t < seconds; t += 0.5) {
        audio::Status s = audio::status(1);
        std::printf("%5.1f s  position %.2f / %.1f  %s%s\n", t, s.position, s.duration, s.paused ? "paused" : "playing", s.ended ? " ended" : "");
        if (t == 2.0) { audio::pause(1); std::printf("  pause\n"); }
        if (t == 3.0) { audio::resume(1); std::printf("  resume\n"); }
        if (t == 4.0) { audio::seek(1, 30.0); std::printf("  seek 30\n"); }
        if (!s.error.empty()) std::printf("  error: %s\n", s.error.c_str());
        wait(0.5);
    }
    audio::stop(1);
    return 0;
}

int play_mp4(const std::string& file, double seconds) {
    video::Result r = video::play(1, file, false);
    if (!r.ok) { std::printf("play failed: %s\n", r.error.c_str()); return 1; }
    uint64_t serial = 0;
    int pictures = 0;
    const auto begin = std::chrono::steady_clock::now();
    double last_print = -1;
    for (;;) {
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        if (t >= seconds) break;
        video::fetch_frame(1, serial, [&](const uint8_t*, int, int) { ++pictures; });
        if (t - last_print >= 0.5) {
            last_print = t;
            video::Status s = video::status(1);
            std::printf("%5.1f s  position %.2f / %.1f  pictures %d  late %d  sound %s%s%s\n", t, s.position, s.duration, pictures,
                        s.dropped, s.audio ? "yes" : "no", s.ended ? "  ended" : "", s.error.empty() ? "" : ("  [" + s.error + "]").c_str());
            if (t > 2.0 && t < 2.6 && !s.paused) { video::pause(1); std::printf("  pause\n"); }
            if (t > 3.0 && s.paused) { video::resume(1); std::printf("  resume\n"); }
            if (t > 4.5 && t < 5.1) { video::seek(1, 60.0); std::printf("  seek 60\n"); }
            if (s.ended) break;
        }
        wait(0.005);
    }
    video::stop(1);
    return 0;
}

// Three sessions at once: two songs and a video. Each position must advance on its own clock, and pausing one must not
// stop the others.
int mix(const std::string& song_a, const std::string& song_b, const std::string& clip) {
    if (!audio::play(1, song_a).ok || !audio::play(2, song_b, 20.0).ok || !video::play(3, clip, false).ok) {
        std::printf("could not start the three\n");
        return 1;
    }
    audio::set_volume(1, 0.5f);
    audio::set_volume(2, 0.5f);
    video::set_volume(3, 0.5f);
    uint64_t serial = 0;
    int pictures = 0;
    bool ok = true;
    double last_a = 0, last_b = 0, last_c = 0;
    for (int step = 0; step < 14; ++step) {
        for (int i = 0; i < 50; ++i) {   // half a second, taking the video pictures
            video::fetch_frame(3, serial, [&](const uint8_t*, int, int) { ++pictures; });
            wait(0.01);
        }
        audio::Status a = audio::status(1), b = audio::status(2);
        video::Status c = video::status(3);
        std::printf("%4.1f s  song A %6.2f %s   song B %6.2f %s   video %6.2f %s  pictures %d\n", (step + 1) * 0.5, a.position,
                    a.paused ? "paused " : "playing", b.position, b.paused ? "paused " : "playing", c.position, c.paused ? "paused " : "playing", pictures);
        auto advances = [&](const char* name, double now, double before, bool should) {
            const bool moved = now > before + 0.2;
            if (should != moved && step > 0) { std::printf("  FAIL %s %s\n", name, should ? "did not advance" : "advanced while paused"); ok = false; }
        };
        // a command at the end of step N shows from step N + 1
        advances("song A", a.position, last_a, step <= 4 || step >= 9);
        advances("song B", b.position, last_b, step <= 6 || step >= 11);
        advances("video", c.position, last_c, true);
        last_a = a.position; last_b = b.position; last_c = c.position;
        if (step == 4) { audio::pause(1); std::printf("  pause song A\n"); }
        if (step == 6) { audio::pause(2); std::printf("  pause song B\n"); }
        if (step == 8) { audio::resume(1); std::printf("  resume song A\n"); }
        if (step == 10) { audio::resume(2); std::printf("  resume song B\n"); }
    }
    audio::shutdown();
    video::shutdown();
    std::printf(ok ? "MIX OK\n" : "MIX FAILED\n");
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: %s probe|mp3|mp4 FILE [SECONDS]  |  %s mix MP3 MP3 MP4\n", argv[0], argv[0]);
        return 2;
    }
    const std::string mode = argv[1];
    const std::string file = argv[2];
    const double seconds = argc > 3 ? std::atof(argv[3]) : 8.0;
    int code = 2;
    if (mode == "probe") code = probe(file);
    else if (mode == "mp3") code = play_mp3(file, seconds);
    else if (mode == "mp4") code = play_mp4(file, seconds);
    else if (mode == "mix" && argc >= 5) code = mix(file, argv[3], argv[4]);
    audio::shutdown();
    video::shutdown();
    return code;
}
