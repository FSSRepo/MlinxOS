#include "media/audio_output.h"

#ifdef _WIN32
#define NOMINMAX
#endif

// Only the playback device is needed: no decoders, encoders, generators or resource manager.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace linweb {
namespace media {

// One AudioOutput is one source of the mixer: a ring buffer in its own format, a converter to the format of the mixer
// (32-bit float, stereo, 48 kHz) and a gain.
struct AudioOutput::Impl {
    ma_pcm_rb ring;
    ma_data_converter converter;
    bool ring_ready = false;
    bool converter_ready = false;
    bool registered = false;
    int rate = 0;
    int channels = 0;
    size_t frame_bytes = 0;

    std::atomic<bool> aborted{false};
    std::atomic<bool> paused{false};
    std::atomic<int64_t> consumed{0};   // frames of the ring the mixer took
    std::atomic<int64_t> base{0};       // value of `consumed` at the last flush
    std::atomic<float> volume{1.0f};
};

namespace {

constexpr ma_uint32 kMixRate = 48000;
constexpr ma_uint32 kMixChannels = 2;
constexpr ma_uint32 kChunk = 512;       // output frames mixed at a time

std::atomic<float> g_level{0.0f};       // what mixer_levels() reports
std::atomic<float> g_bass{0.0f};

// The sound device and the sources that play through it. The device exists while at least one source is open.
class Mixer {
public:
    static Mixer& get() {
        static Mixer mixer;
        return mixer;
    }

    bool add(AudioOutput::Impl* source, std::string& error) {
        std::lock_guard<std::mutex> guard(lock_);
        if (!device_ready_) {
            ma_device_config config = ma_device_config_init(ma_device_type_playback);
            config.playback.format = ma_format_f32;
            config.playback.channels = kMixChannels;
            config.sampleRate = kMixRate;
            config.dataCallback = &Mixer::data_callback;
            config.pUserData = this;
            config.periodSizeInMilliseconds = 10;
            config.periods = 3;
            if (ma_device_init(nullptr, &config, &device_) != MA_SUCCESS) {
                error = "no sound device available";
                return false;
            }
            if (ma_device_start(&device_) != MA_SUCCESS) {
                ma_device_uninit(&device_);
                error = "cannot start the sound device";
                return false;
            }
            device_ready_ = true;
        }
        sources_.push_back(source);
        return true;
    }

    void remove(AudioOutput::Impl* source) {
        std::lock_guard<std::mutex> guard(lock_);
        sources_.erase(std::remove(sources_.begin(), sources_.end(), source), sources_.end());
        if (sources_.empty() && device_ready_) {
            ma_device_uninit(&device_);
            device_ready_ = false;
            g_level = 0.0f;
            g_bass = 0.0f;
        }
    }

    std::mutex& lock() { return lock_; }

private:
    // Sums what each source has, one chunk at a time. Runs on the device thread.
    static void data_callback(ma_device* device, void* output, const void*, ma_uint32 frame_count) {
        Mixer* self = static_cast<Mixer*>(device->pUserData);
        float* out = static_cast<float*>(output);
        std::lock_guard<std::mutex> guard(self->lock_);
        ma_uint32 done = 0;
        double sum = 0.0, sum_low = 0.0;
        while (done < frame_count) {
            const ma_uint32 chunk = std::min(kChunk, frame_count - done);
            float* mix = out + static_cast<size_t>(done) * kMixChannels;
            std::memset(mix, 0, static_cast<size_t>(chunk) * kMixChannels * sizeof(float));
            for (AudioOutput::Impl* source : self->sources_) {
                if (!source->paused && !source->aborted) mix_source(*source, mix, chunk);
            }
            // several loud sources can add up past full scale: the sum is limited, not wrapped around
            for (ma_uint32 i = 0; i < chunk * kMixChannels; ++i) mix[i] = std::max(-1.0f, std::min(1.0f, mix[i]));
            // loudness of the mix: RMS; bass: RMS of a one-pole low-pass (about 200 Hz)
            for (ma_uint32 i = 0; i < chunk; ++i) {
                const double x = 0.5 * (mix[2 * i] + mix[2 * i + 1]);
                self->low_pass_ += 0.026 * (x - self->low_pass_);
                sum += x * x;
                sum_low += self->low_pass_ * self->low_pass_;
            }
            done += chunk;
        }
        if (frame_count > 0) {
            g_level = static_cast<float>(std::min(1.0, std::sqrt(sum / frame_count) * 3.5));
            g_bass = static_cast<float>(std::min(1.0, std::sqrt(sum_low / frame_count) * 5.0));
        }
    }

    // Adds `frames` output frames of one source to `mix`; what the source does not have is silence.
    static void mix_source(AudioOutput::Impl& source, float* mix, ma_uint32 frames) {
        float converted[kChunk * kMixChannels];
        const float gain = source.volume;
        ma_uint32 done = 0;
        while (done < frames) {
            const ma_uint32 want = frames - done;
            ma_uint64 needed = 0;
            if (ma_data_converter_get_required_input_frame_count(&source.converter, want, &needed) != MA_SUCCESS || needed == 0) break;
            ma_uint32 available = static_cast<ma_uint32>(needed);
            void* input = nullptr;
            if (ma_pcm_rb_acquire_read(&source.ring, &available, &input) != MA_SUCCESS || available == 0) break;
            ma_uint64 in_count = available;
            ma_uint64 out_count = want;
            ma_data_converter_process_pcm_frames(&source.converter, input, &in_count, converted, &out_count);
            ma_pcm_rb_commit_read(&source.ring, static_cast<ma_uint32>(in_count));
            source.consumed.fetch_add(static_cast<int64_t>(in_count));
            float* target = mix + static_cast<size_t>(done) * kMixChannels;
            for (ma_uint64 i = 0; i < out_count * kMixChannels; ++i) target[i] += converted[i] * gain;
            done += static_cast<ma_uint32>(out_count);
            if (in_count == 0 && out_count == 0) break;
        }
    }

    std::mutex lock_;
    double low_pass_ = 0.0;     // state of the bass filter of the levels
    ma_device device_;
    bool device_ready_ = false;
    std::vector<AudioOutput::Impl*> sources_;
};

void drain_ring(ma_pcm_rb& ring) {
    for (;;) {
        ma_uint32 n = ma_pcm_rb_available_read(&ring);
        void* data = nullptr;
        if (n == 0 || ma_pcm_rb_acquire_read(&ring, &n, &data) != MA_SUCCESS || n == 0) break;
        ma_pcm_rb_commit_read(&ring, n);
    }
}

} // namespace

AudioOutput::AudioOutput() : impl_(new Impl) {}

AudioOutput::~AudioOutput() {
    close();
}

bool AudioOutput::open(int sample_rate, int channels, int buffer_ms, std::string& error) {
    close();
    Impl& im = *impl_;
    if (sample_rate <= 0 || channels <= 0 || channels > 8) { error = "unsupported audio format"; return false; }
    im.rate = sample_rate;
    im.channels = channels;
    im.frame_bytes = sizeof(int16_t) * static_cast<size_t>(channels);
    im.aborted = false;
    im.consumed = 0;
    im.base = 0;

    const ma_uint32 frames = static_cast<ma_uint32>(std::max(100, buffer_ms)) * static_cast<ma_uint32>(sample_rate) / 1000;
    if (ma_pcm_rb_init(ma_format_s16, static_cast<ma_uint32>(channels), frames, nullptr, nullptr, &im.ring) != MA_SUCCESS) {
        error = "cannot allocate the audio buffer";
        return false;
    }
    im.ring_ready = true;
    ma_data_converter_config config = ma_data_converter_config_init(ma_format_s16, ma_format_f32, static_cast<ma_uint32>(channels),
                                                                     kMixChannels, static_cast<ma_uint32>(sample_rate), kMixRate);
    if (ma_data_converter_init(&config, nullptr, &im.converter) != MA_SUCCESS) {
        close();
        error = "cannot convert this audio format";
        return false;
    }
    im.converter_ready = true;
    if (!Mixer::get().add(&im, error)) {
        close();
        return false;
    }
    im.registered = true;
    return true;
}

bool AudioOutput::is_open() const {
    return impl_->registered;
}

void AudioOutput::close() {
    Impl& im = *impl_;
    im.aborted = true;
    if (im.registered) {
        Mixer::get().remove(&im);    // after this the mixer does not touch the source
        im.registered = false;
    }
    if (im.converter_ready) { ma_data_converter_uninit(&im.converter, nullptr); im.converter_ready = false; }
    if (im.ring_ready) { ma_pcm_rb_uninit(&im.ring); im.ring_ready = false; }
}

bool AudioOutput::write(const int16_t* samples, size_t frames) {
    Impl& im = *impl_;
    while (frames > 0) {
        if (im.aborted || !im.ring_ready) return false;
        ma_uint32 n = static_cast<ma_uint32>(std::min<size_t>(frames, 4096));
        void* room = nullptr;
        if (ma_pcm_rb_acquire_write(&im.ring, &n, &room) == MA_SUCCESS && n > 0) {
            std::memcpy(room, samples, static_cast<size_t>(n) * im.frame_bytes);
            ma_pcm_rb_commit_write(&im.ring, n);
            samples += static_cast<size_t>(n) * static_cast<size_t>(im.channels);
            frames -= n;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
    }
    return true;
}

void AudioOutput::abort() {
    impl_->aborted = true;
}

void AudioOutput::pause() {
    impl_->paused = true;
}

void AudioOutput::resume() {
    impl_->paused = false;
}

bool AudioOutput::paused() const {
    return impl_->paused;
}

void AudioOutput::set_volume(float volume) {
    impl_->volume = std::max(0.0f, std::min(1.0f, volume));
}

float AudioOutput::volume() const {
    return impl_->volume;
}

void AudioOutput::flush() {
    Impl& im = *impl_;
    if (!im.registered) return;
    // with the mixer lock held the device thread is not reading this source: the buffer can be emptied from here
    std::lock_guard<std::mutex> guard(Mixer::get().lock());
    drain_ring(im.ring);
    im.base = im.consumed.load();
}

void AudioOutput::drain(int timeout_ms) {
    Impl& im = *impl_;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (im.ring_ready && !im.aborted && !im.paused && ma_pcm_rb_available_read(&im.ring) > 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

size_t AudioOutput::buffered_frames() const {
    return impl_->ring_ready ? ma_pcm_rb_available_read(&impl_->ring) : 0;
}

double AudioOutput::played_seconds() const {
    if (impl_->rate <= 0) return 0.0;
    return static_cast<double>(impl_->consumed.load() - impl_->base.load()) / impl_->rate;
}

int AudioOutput::sample_rate() const { return impl_->rate; }
int AudioOutput::channels() const { return impl_->channels; }

void mixer_levels(float& level, float& bass) {
    level = g_level;
    bass = g_bass;
}

} // namespace media
} // namespace linweb
