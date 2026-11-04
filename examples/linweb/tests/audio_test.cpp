// Host-side checks for src/system/audio.cpp (Linux). Playback itself needs a sound card
// and is covered by the QEMU steps in the README.
//   g++ -std=c++17 -Isrc -Isrc/media/mp3 -Iexternal/openmp3 tests/audio_test.cpp src/system/audio.cpp src/system/storage.cpp \
//       src/media/mp3/*.cpp external/openmp3/*.cpp -o /tmp/audio_test && /tmp/audio_test
#include "system/audio.h"
#include "system/storage.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace linweb;

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

static void write_text(const std::string& path, const std::string& text) {
    std::ofstream out(path);
    out << text;
}

int main(int argc, char** argv) {
    if (audio::is_helper_invocation(argc, argv)) return audio::helper_main(argc, argv);   // the player re-runs this binary
    char tmpl[] = "/tmp/linweb-audio-XXXXXX";
    std::string base = mkdtemp(tmpl);

    write_text(base + "/cards",
               " 0 [HDMI           ]: HDA-Intel - HDA Intel HDMI\n"
               "                      HDA Intel HDMI at 0xf7a14000 irq 29\n"
               " 1 [PCH            ]: HDA-Intel - HDA Intel PCH\n"
               "                      HDA Intel PCH at 0xf7a10000 irq 30\n"
               " 2 [Loop           ]: Dummy - Loopback\n"
               "                      Loopback 1\n");
    write_text(base + "/pcm",
               "00-03: HDMI 0 : HDMI 0 : playback 1\n"
               "00-07: HDMI 1 : HDMI 1 : playback 1\n"
               "01-00: ALC3234 Analog : ALC3234 Analog : playback 1 : capture 1\n"
               "02-00: Loopback : Loopback PCM : capture 1\n");
    std::vector<audio::Card> cards = audio::list_cards(base + "/cards", base + "/pcm");
    CHECK(cards.size() == 3);
    if (cards.size() == 3) {
        CHECK(cards[0].index == 0 && cards[0].id == "HDMI" && cards[0].playback && cards[0].hdmi);
        CHECK(cards[1].index == 1 && cards[1].id == "PCH" && cards[1].name == "HDA Intel PCH" && cards[1].playback && !cards[1].hdmi);
        CHECK(cards[2].index == 2 && cards[2].id == "Loop" && !cards[2].playback && !cards[2].hdmi);
    }
    CHECK(audio::list_cards(base + "/missing", base + "/missing").empty());

    audio::Volume v = audio::parse_amixer(
        "Simple mixer control 'Master',0\n  Capabilities: pvolume pswitch\n  Playback channels: Front Left - Front Right\n"
        "  Limits: Playback 0 - 87\n  Mono:\n  Front Left: Playback 61 [70%] [-13.50dB] [on]\n"
        "  Front Right: Playback 61 [70%] [-13.50dB] [on]\n");
    CHECK(v.ok && v.percent == 70 && !v.muted);
    v = audio::parse_amixer("  Front Left: Playback 0 [0%] [-95.25dB] [off]\n");
    CHECK(v.ok && v.percent == 0 && v.muted);
    CHECK(!audio::parse_amixer("amixer: Unable to find simple control").ok);

    // Only files inside the mount root may be played.
    storage::set_mount_root(base);
    CHECK(!audio::play_mp3("/etc/passwd", -1).ok);
    CHECK(!audio::play_mp3(base + "/../etc/passwd", -1).ok);
    write_text(base + "/fake.mp3", "this is not an mp3");
    audio::Result r = audio::play_mp3(base + "/fake.mp3", -1);
    CHECK(!r.ok);
    CHECK(!audio::status().playing);
    CHECK(audio::stop().ok);

    // Optional streaming check: audio_test song.mp3. A fake aplay that consumes one second of
    // audio per second stands in for the sound card, so a decoder that read ahead without
    // limit would finish (and exit) long before the song does.
    if (argc > 1) {
        write_text(base + "/aplay", "#!/bin/sh\nwhile head -c 176400 >/dev/null; do sleep 1; done\n");
        chmod((base + "/aplay").c_str(), 0755);
        setenv("PATH", (base + ":" + std::getenv("PATH")).c_str(), 1);
        mkdir("/tmp/linweb-snd-fake", 0755);
        std::string song = base + "/song.mp3";
        CHECK(std::system(("cp '" + std::string(argv[1]) + "' '" + song + "'").c_str()) == 0);
        r = audio::play_mp3(song, -1, 500);
        if (!r.ok) std::printf("play failed: %s\n", r.error.c_str());
        CHECK(r.ok);
        sleep(3);
        audio::PlayerStatus s = audio::status();
        CHECK(s.playing);                                   // still decoding/feeding after 3 s
        CHECK(s.position > 2.5 && s.position < 4.0);
        CHECK(s.sample_rate == 44100 && s.channels == 2 && s.duration > 60);
        std::printf("playing=%d position=%.1f duration=%.1f\n", s.playing, s.position, s.duration);
        CHECK(audio::pause().ok);
        double frozen = audio::status().position;
        sleep(1);
        CHECK(audio::status().paused && audio::status().position - frozen < 0.1);
        CHECK(audio::resume().ok);
        CHECK(audio::stop().ok);
        CHECK(!audio::status().playing);
    }

    std::printf(g_failures ? "%d check(s) failed\n" : "all audio checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
