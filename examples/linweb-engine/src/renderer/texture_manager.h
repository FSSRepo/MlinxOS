#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "gl_wrapper.h"

namespace linweb {

struct Texture {
    GLuint id = 0;
    int width = 0;                  // the natural size of the picture, as the file says: layout and shadows use it
    int height = 0;
    int gpu_width = 0;              // the size of the texture on the GPU: the natural size reduced to the limit
    int gpu_height = 0;
};

class TextureManager {
public:
    static TextureManager& get() {
        static TextureManager instance;
        return instance;
    }

    // The texture of a picture file, for drawing. A small file is decoded at once. A large one (a photo) is decoded on a
    // worker thread: until it is ready the id is 0 and nothing is drawn, so the page does not freeze while it loads.
    Texture get_texture(const std::string& path);
    // The size of a picture file for the layout: from the texture when it is there, otherwise from the header of the
    // file, so a picture that is still loading already has its box.
    bool image_size(const std::string& path, int& width, int& height);

    // The longest side of a texture on the GPU: a larger picture is reduced before the upload. 0 means the driver
    // limit (queried once, never above 8192). The default is 2048.
    void set_max_side(int side);
    // The memory the large textures may take before the least recently used ones are freed. The default is 256 MB;
    // 0 keeps it.
    void set_budget_mb(size_t megabytes);

    // Call once per frame from the thread that owns the GL context. Makes textures of the pictures the worker has
    // decoded (within `budget_ms`), frees the least recently used large ones past the memory budget, and returns true
    // when a picture became available (the page needs a new frame).
    bool upload_pending(double budget_ms = 4.0);
    // Pictures are still being decoded: frames must keep coming until they are ready.
    bool loading() const { return in_flight_ > 0; }
    // This picture is being decoded: the renderer draws a placeholder in its box meanwhile.
    bool is_loading(const std::string& path) const { return jobs.find(path) != jobs.end(); }

    Texture get_svg_texture(const std::string& path, int width, int height, const std::string& color);
    Texture get_inline_svg_texture(const std::string& content, int width, int height, const std::string& color);
    void clear();

private:
    TextureManager() = default;
    ~TextureManager();

    // A picture being decoded by the worker
    struct Job {
        std::string path;
        std::atomic<int> state{0};          // 0 waiting, 1 decoding, 2 decoded (waiting for the upload), 3 failed
        int width = 0, height = 0, channels = 0;
        int side = 0;                       // the longest side to reduce the picture to (the worker cannot query GL)
        std::vector<unsigned char> pixels;  // the decoded picture, already reduced when it is too large
    };

    struct Usage {
        uint64_t frame = 0;                 // the last frame the picture was asked for
        size_t bytes = 0;
        std::filesystem::file_time_type modified{};     // when the file was last written (file paths only)
        std::chrono::steady_clock::time_point checked{}; // the last time the file system was asked about it
    };

    Texture upload(const std::string& path, unsigned char* data, int width, int height, int channels);
    // True when the file of the cached picture was written again: the stale texture is freed here (on the GL thread)
    // and the caller loads the picture anew. The file system is asked at most once a second.
    bool file_changed(const std::string& path);
    // Reduces a picture to `side` on its longest side, in place. False when it already fits (or the resize fails).
    static bool shrink_to_side(std::vector<unsigned char>& pixels, int& w, int& h, int side);
    int effective_max_side();
    void start_worker();
    void worker_main();
    void evict_if_needed();

    std::unordered_map<std::string, Texture> textures;
    std::unordered_map<std::string, Usage> usage;
    std::unordered_map<std::string, std::shared_ptr<Job>> jobs;     // main thread only
    std::unordered_set<std::string> failed;
    std::unordered_map<std::string, std::pair<int, int>> sizes;     // sizes read from the headers
    uint64_t frame_ = 0;
    int in_flight_ = 0;
    int max_side_ = 2048;
    size_t budget_bytes_ = 256u * 1024 * 1024;  // the memory of the large textures before the least used ones are freed
    int driver_max_side_ = 0;               // GL_MAX_TEXTURE_SIZE: queried the first time a texture needs it

    std::thread worker_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<std::shared_ptr<Job>> queue_;
    bool stop_ = false;
};

} // namespace linweb
