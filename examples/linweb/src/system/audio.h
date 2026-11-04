#pragma once
#include <string>
#include <vector>

namespace linweb {
namespace audio {

struct Result {
    bool ok = false;
    std::string error;
};

struct Card {
    int index = 0;
    std::string id;         // e.g. "Intel"
    std::string name;       // e.g. "HDA Intel PCH"
    bool playback = false;  // has at least one playback PCM
    bool hdmi = false;      // every playback PCM is HDMI/DisplayPort (no speakers or jack)
};

struct Volume {
    bool ok = false;
    std::string error;
    std::string control;    // mixer control that answered, e.g. "Master"
    int percent = 0;
    bool muted = false;
};

struct PlayerStatus {
    bool playing = false;   // a track is loaded and its helper is still running
    bool paused = false;
    std::string path;
    double position = 0;    // seconds played (excludes time spent paused)
    double duration = 0;    // estimated from file size and bitrate (exact for CBR files)
    int sample_rate = 0;
    int channels = 0;
    std::string error;      // set once when the helper ended abnormally (for example no sound device)
};

// Sound cards from /proc/asound/cards, with playback support from /proc/asound/pcm.
std::vector<Card> list_cards(const std::string& cards_file = "/proc/asound/cards",
                             const std::string& pcm_file = "/proc/asound/pcm");

// Parse the output of `amixer sget <control>`: the first "[NN%]" and "[on|off]" win.
Volume parse_amixer(const std::string& output);

// Mixer access through amixer (Master first, then PCM). Percent is clamped to 0..100.
Volume get_volume(int card);
Volume set_volume(int card, int percent);
Result set_mute(int card, bool muted);

// MP3 playback. The file must be inside the storage mount root. The decoder runs in a
// helper process (this executable with --play-mp3) that decodes frame by frame and writes
// to aplay through a pipe sized to `margin_ms` of audio, so it never decodes more than
// that margin ahead of the sound card. card < 0 uses ALSA's default device.
Result play_mp3(const std::string& path, int card, int margin_ms = 1000);
Result stop();
Result pause();
Result resume();
PlayerStatus status();

// Entry point of the helper: args are "--play-mp3 PATH CARD MARGIN_MS". Returns an exit code.
bool is_helper_invocation(int argc, char** argv);
int helper_main(int argc, char** argv);

} // namespace audio
} // namespace linweb
