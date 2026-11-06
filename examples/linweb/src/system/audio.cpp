#include "system/audio.h"

#ifdef __linux__
#include "media/mp3/mp3_stream.h"
#include "system/process.h"
#include "system/storage.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <spawn.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace linweb {
namespace audio {

namespace {

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

std::string trim(const std::string& text) {
    size_t a = text.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = text.find_last_not_of(" \t\r\n");
    return text.substr(a, b - a + 1);
}

using process::find_tool;
using process::run_capture;

const char* kControls[] = {"Master", "PCM", "Speaker", "Headphone"};

Volume query_control(int card, const char* control) {
    std::string out;
    int code = 0;
    Volume v;
    if (!run_capture({"amixer", "-c", std::to_string(card), "sget", control}, out, code)) {
        v.error = trim(out);
        return v;
    }
    if (code != 0) {
        v.error = trim(out).empty() ? "amixer failed" : trim(out);
        return v;
    }
    v = parse_amixer(out);
    v.control = control;
    return v;
}

// ---- player state (one track at a time) --------------------------------------
struct Player {
    pid_t pid = 0;
    std::string path;
    bool paused = false;
    int sample_rate = 0;
    int channels = 0;
    double duration = 0;
    double played = 0;                                  // seconds accumulated before the last resume
    std::chrono::steady_clock::time_point since;        // start of the current running span
};

Player g_player;
std::string g_last_error;   // why the last helper ended abnormally, reported once by status()

const char* helper_error(int code) {
    switch (code) {
        case 3: return "cannot decode the file";
        case 4: return "cannot create the audio pipe";
        case 5: return "cannot start aplay";
        case 6: return "aplay failed (no sound device?)";
        default: return "the player stopped unexpectedly";
    }
}

// Reap the helper when it exits; returns true while it is still running.
bool refresh_player() {
    if (g_player.pid <= 0) return false;
    int status = 0;
    pid_t done = waitpid(g_player.pid, &status, WNOHANG);
    if (done == g_player.pid || (done < 0 && errno == ECHILD)) {
        if (done == g_player.pid && !(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
            g_last_error = WIFEXITED(status) ? helper_error(WEXITSTATUS(status)) : "the player stopped unexpectedly";
        }
        g_player.pid = 0;
        g_player.paused = false;
        return false;
    }
    return true;
}

double elapsed() {
    double seconds = g_player.played;
    if (!g_player.paused) {
        seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - g_player.since).count();
    }
    return seconds;
}

} // namespace

std::vector<Card> list_cards(const std::string& cards_file, const std::string& pcm_file) {
    std::vector<Card> cards;
    std::ifstream in(cards_file);
    std::string line;
    while (std::getline(in, line)) {
        // " 0 [PCH            ]: HDA-Intel - HDA Intel PCH"
        size_t open = line.find('[');
        size_t close = line.find(']');
        if (open == std::string::npos || close == std::string::npos || close < open) continue;
        Card card;
        card.index = std::atoi(line.c_str());
        card.id = trim(line.substr(open + 1, close - open - 1));
        size_t dash = line.find(" - ", close);
        card.name = trim(dash == std::string::npos ? line.substr(close + 1) : line.substr(dash + 3));
        cards.push_back(card);
    }
    std::ifstream pcm(pcm_file);
    std::vector<bool> has_analog(cards.size(), false);
    while (std::getline(pcm, line)) {
        // "00-00: Generic Analog : Generic Analog : playback 1 : capture 1"
        if (line.find("playback") == std::string::npos) continue;
        int index = std::atoi(line.c_str());
        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return std::tolower(ch); });
        bool digital = lower.find("hdmi") != std::string::npos || lower.find("displayport") != std::string::npos ||
                       lower.find(" dp ") != std::string::npos || lower.find(": dp") != std::string::npos;
        for (size_t i = 0; i < cards.size(); ++i) {
            if (cards[i].index != index) continue;
            cards[i].playback = true;
            if (!digital) has_analog[i] = true;
        }
    }
    for (size_t i = 0; i < cards.size(); ++i) cards[i].hdmi = cards[i].playback && !has_analog[i];
    return cards;
}

Volume parse_amixer(const std::string& output) {
    Volume v;
    size_t pct = output.find('%');
    while (pct != std::string::npos) {
        size_t open = output.rfind('[', pct);
        if (open != std::string::npos) {
            v.percent = std::atoi(output.c_str() + open + 1);
            v.ok = true;
            break;
        }
        pct = output.find('%', pct + 1);
    }
    if (!v.ok) {
        v.error = "no volume in amixer output";
        return v;
    }
    v.muted = output.find("[off]") != std::string::npos;
    return v;
}

// Laptop codecs keep separate switches for the speakers and the jack; a freshly booted mixer
// can leave them off even when Master is up. Best effort: controls that do not exist are ignored.
static void unmute_outputs(int card, bool unmute) {
    if (!unmute) return;
    std::string out;
    int code = 0;
    for (const char* control : {"Speaker", "Headphone", "Front", "PCM"}) {
        run_capture({"amixer", "-c", std::to_string(card), "-q", "sset", control, "unmute"}, out, code);
    }
}

Volume get_volume(int card) {
    Volume last;
    last.error = "no mixer control found";
    for (const char* control : kControls) {
        Volume v = query_control(card, control);
        if (v.ok) return v;
        if (!v.error.empty()) last = v;
    }
    return last;
}

Volume set_volume(int card, int percent) {
    percent = std::max(0, std::min(100, percent));
    Volume current = get_volume(card);
    if (!current.ok) return current;
    std::string out;
    int code = 0;
    // sset with a level also unmutes switches that are tied to the control
    if (!run_capture({"amixer", "-c", std::to_string(card), "sset", current.control,
                      std::to_string(percent) + "%", "unmute"}, out, code) || code != 0) {
        current.ok = false;
        current.error = trim(out).empty() ? "amixer failed" : trim(out);
        return current;
    }
    unmute_outputs(card, percent > 0);
    return get_volume(card);
}

Result set_mute(int card, bool muted) {
    Volume current = get_volume(card);
    if (!current.ok) return fail(current.error);
    std::string out;
    int code = 0;
    if (!run_capture({"amixer", "-c", std::to_string(card), "sset", current.control, muted ? "mute" : "unmute"},
                     out, code) || code != 0) {
        return fail(trim(out).empty() ? "amixer failed" : trim(out));
    }
    unmute_outputs(card, !muted);
    return success();
}

Result play_mp3(const std::string& path, int card, int margin_ms) {
    stop();
    g_last_error.clear();
    std::string resolved;
    storage::Result allowed = storage::resolve_existing(path, resolved);
    if (!allowed.ok) return fail(allowed.error);
    struct stat st;
    if (stat(resolved.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return fail("not a regular file");
    if (find_tool("aplay").empty()) return fail("aplay is not installed in this system");

    // Validate the file and learn its format before launching the helper.
    Mp3Stream probe;
    if (!probe.open(resolved.c_str())) return fail("not a playable MP3 file");

    margin_ms = std::max(100, std::min(10000, margin_ms));
    std::vector<std::string> args = {"linweb", "--play-mp3", resolved, std::to_string(card), std::to_string(margin_ms)};
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(&a[0]);
    argv.push_back(nullptr);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);   // own group: stop/pause reach aplay too
    posix_spawnattr_setpgroup(&attr, 0);
    pid_t pid = 0;
    int rc = posix_spawn(&pid, "/proc/self/exe", nullptr, &attr, argv.data(), environ);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) return fail(std::string("cannot start the player: ") + std::strerror(rc));

    g_player = Player();
    g_player.pid = pid;
    g_player.path = resolved;
    g_player.sample_rate = probe.sample_rate();
    g_player.channels = probe.channels();
    if (probe.bitrate() > 0) g_player.duration = static_cast<double>(st.st_size) * 8.0 / probe.bitrate();
    g_player.since = std::chrono::steady_clock::now();
    return success();
}

Result stop() {
    if (!refresh_player()) return success();
    kill(-g_player.pid, SIGCONT);
    kill(-g_player.pid, SIGTERM);
    waitpid(g_player.pid, nullptr, 0);
    g_player = Player();
    g_last_error.clear();
    return success();
}

Result pause() {
    if (!refresh_player()) return fail("nothing is playing");
    if (g_player.paused) return success();
    g_player.played = elapsed();
    g_player.paused = true;
    kill(-g_player.pid, SIGSTOP);
    return success();
}

Result resume() {
    if (!refresh_player()) return fail("nothing is playing");
    if (!g_player.paused) return success();
    g_player.paused = false;
    g_player.since = std::chrono::steady_clock::now();
    kill(-g_player.pid, SIGCONT);
    return success();
}

PlayerStatus status() {
    PlayerStatus s;
    if (!refresh_player()) {
        s.error = g_last_error;
        g_last_error.clear();
        return s;
    }
    s.playing = true;
    s.paused = g_player.paused;
    s.path = g_player.path;
    s.position = elapsed();
    s.duration = g_player.duration;
    s.sample_rate = g_player.sample_rate;
    s.channels = g_player.channels;
    return s;
}

bool is_helper_invocation(int argc, char** argv) {
    return argc >= 5 && std::strcmp(argv[1], "--play-mp3") == 0;
}

// Helper process: decode one frame at a time and feed aplay through a pipe that holds at
// most `margin` of audio, so the write blocks (and decoding pauses) once the margin is full.
int helper_main(int argc, char** argv) {
    if (!is_helper_invocation(argc, argv)) return 2;
    const char* path = argv[2];
    int card = std::atoi(argv[3]);
    int margin_ms = std::max(100, std::atoi(argv[4]));
    signal(SIGPIPE, SIG_IGN);

    Mp3Stream stream;
    if (!stream.open(path)) return 3;
    const int channels = stream.channels();

    int fds[2];
    if (pipe(fds) != 0) return 4;
#ifdef F_SETPIPE_SZ
    long margin_bytes = static_cast<long>(stream.sample_rate()) * channels * 2 * margin_ms / 1000;
    fcntl(fds[1], F_SETPIPE_SZ, static_cast<int>(std::max(4096L, margin_bytes)));
#endif

    std::string aplay = find_tool("aplay");
    std::vector<std::string> args = {"aplay", "-q", "-t", "raw", "-f", "S16_LE",
                                     "-r", std::to_string(stream.sample_rate()), "-c", std::to_string(channels)};
    if (card >= 0) {
        args.push_back("-D");
        args.push_back("plughw:" + std::to_string(card) + ",0");
    }
    args.push_back("-");
    std::vector<char*> aplay_argv;
    for (std::string& a : args) aplay_argv.push_back(&a[0]);
    aplay_argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[0], STDIN_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_addclose(&actions, fds[1]);
    pid_t child = 0;
    int rc = posix_spawn(&child, aplay.c_str(), &actions, nullptr, aplay_argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[0]);
    if (rc != 0) return 5;

    std::vector<int16_t> pcm(static_cast<size_t>(Mp3Stream::kFrameSamples) * channels);
    bool broken = false;
    while (!broken && stream.read_frame(pcm.data())) {
        const char* bytes = reinterpret_cast<const char*>(pcm.data());
        size_t left = pcm.size() * sizeof(int16_t);
        while (left > 0) {
            ssize_t n = write(fds[1], bytes, left);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { broken = true; break; }   // aplay is gone
            bytes += n;
            left -= static_cast<size_t>(n);
        }
    }
    close(fds[1]);
    int status = 0;
    waitpid(child, &status, 0);   // lets aplay drain what is still buffered
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : 6;
}

} // namespace audio
} // namespace linweb

#else // !__linux__

namespace linweb {
namespace audio {

namespace {
Result unsupported() {
    Result r;
    r.error = "audio is only supported on Linux";
    return r;
}
}

std::vector<Card> list_cards(const std::string&, const std::string&) { return {}; }

Volume parse_amixer(const std::string&) { return Volume(); }
Volume get_volume(int) { Volume v; v.error = "audio is only supported on Linux"; return v; }
Volume set_volume(int, int) { return get_volume(0); }
Result set_mute(int, bool) { return unsupported(); }
Result play_mp3(const std::string&, int, int) { return unsupported(); }
Result stop() { return Result{true, ""}; }
Result pause() { return unsupported(); }
Result resume() { return unsupported(); }
PlayerStatus status() { return PlayerStatus(); }
bool is_helper_invocation(int, char**) { return false; }
int helper_main(int, char**) { return 2; }

} // namespace audio
} // namespace linweb

#endif
