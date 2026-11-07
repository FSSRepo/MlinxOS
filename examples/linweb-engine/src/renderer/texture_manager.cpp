#include "renderer/texture_manager.h"
#include "renderer/svg_loader.h"
#include "network/http_client.h"
#include "media/byte_reader.h"
#include "app/app_state.h"
#include "utils/memory_files.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace linweb {

namespace {
// A picture of a page held in memory (memory_files) or of a file: decoded from the bytes, or read by stb
unsigned char* load_picture(const std::string& path, int& width, int& height, int& channels) {
    if (auto bytes = memory_files::find(path))
        return stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes->data()), static_cast<int>(bytes->size()), &width, &height, &channels, 0);
    return stbi_load(path.c_str(), &width, &height, &channels, 0);
}

// A file under this size is decoded where it is asked for: it is quick, and an icon must not appear a frame late
const long kAsyncBytes = 384 * 1024;
// Past this much picture memory (with its mipmaps) the least recently used large textures are freed
const size_t kLargeTextureBytes = 2u * 1024 * 1024;

long file_size(const std::string& path) {
    if (auto bytes = memory_files::find(path)) return static_cast<long>(bytes->size());
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return -1;
    return static_cast<long>(file.tellg());
}

// The natural size of a picture from the network: a URL has no header to read before its bytes arrive, so the worker
// remembers the size when it decodes the picture and image_size answers from then on.
std::mutex url_size_mutex;
std::unordered_map<std::string, std::pair<int, int>> url_natural_size;

void remember_url_size(const std::string& url, int width, int height) {
    std::lock_guard<std::mutex> lock(url_size_mutex);
    url_natural_size[url] = std::make_pair(width, height);
}

bool known_url_size(const std::string& url, int& width, int& height) {
    std::lock_guard<std::mutex> lock(url_size_mutex);
    auto found = url_natural_size.find(url);
    if (found == url_natural_size.end()) return false;
    width = found->second.first;
    height = found->second.second;
    return true;
}

// The bytes of a picture on the network, decoded in memory. A failed request or decode is reported in `error`.
unsigned char* load_url(const std::string& url, int& width, int& height, int& channels, std::string& error) {
    try {
        // One client per thread that downloads: its connection is kept for the next picture of the page
        thread_local HttpClient client;
        const HttpResponse response = client.get(url);
        unsigned char* data = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(response.body.data()),
                                                    static_cast<int>(response.body.size()), &width, &height, &channels, 0);
        if (!data) error = stbi_failure_reason() ? stbi_failure_reason() : "cannot decode the picture";
        return data;
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
}

// stbir_resize_uint8 takes 4 channels, so a picture of another depth is widened before the resize
void widen_to_rgba(std::vector<unsigned char>& pixels, int width, int height, int channels) {
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<unsigned char> rgba(count * 4);
    for (size_t i = 0; i < count; ++i) {
        const unsigned char* source = &pixels[i * channels];
        unsigned char* target = &rgba[i * 4];
        if (channels == 1) {
            target[0] = target[1] = target[2] = source[0];
            target[3] = 255;
        } else if (channels == 2) {
            target[0] = target[1] = target[2] = source[0];
            target[3] = source[1];
        } else {
            target[0] = source[0];
            target[1] = source[1];
            target[2] = source[2];
            target[3] = channels == 4 ? source[3] : 255;
        }
    }
    pixels.swap(rgba);
}

// One line per texture when LINWEB_LOG_TEXTURES is set: the size in the file and the size that reaches the GPU
void log_texture(const std::string& path, int natural_width, int natural_height, int gpu_width, int gpu_height) {
    static const bool enabled = std::getenv("LINWEB_LOG_TEXTURES") != nullptr;
    if (!enabled) return;
    std::cerr << "texture " << path << " natural " << natural_width << "x" << natural_height << " gpu " << gpu_width
              << "x" << gpu_height << std::endl;
}
} // namespace

TextureManager::~TextureManager() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    queue_cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    clear();
}

void TextureManager::start_worker() {
    if (worker_.joinable()) return;
    worker_ = std::thread([this] { worker_main(); });
}

// The decoding thread: takes the pictures in the order they were asked for. It only touches its own Job and stb_image;
// the textures are made by the thread that owns the GL context (upload_pending).
void TextureManager::worker_main() {
    for (;;) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            job = queue_.front();
            queue_.pop_front();
        }
        job->state = 1;
        int width = 0, height = 0, channels = 0;
        unsigned char* data = nullptr;
        if (media::is_url(job->path)) {
            // a URL is downloaded and decoded here, never on the draw thread: the server may take seconds to answer
            std::string error;
            data = load_url(job->path, width, height, channels, error);
            if (!data) {
                std::cerr << "Failed to load texture: " << job->path << " Error: " << error << std::endl;
                job->state = 3;
                continue;
            }
            remember_url_size(job->path, width, height);
        } else {
            data = load_picture(job->path, width, height, channels);
            if (!data) {
                job->state = 3;
                continue;
            }
        }
        // The reduction happens here, off the GL thread (no GL is needed): the upload gets the size it returns.
        std::vector<unsigned char> pixels(data, data + static_cast<size_t>(width) * static_cast<size_t>(height) * channels);
        stbi_image_free(data);
        if (std::max(width, height) > job->side) {
            if (channels != 4) {
                widen_to_rgba(pixels, width, height, channels);
                channels = 4;
            }
            shrink_to_side(pixels, width, height, job->side);
        }
        job->width = width;
        job->height = height;
        job->channels = channels;
        job->pixels = std::move(pixels);
        job->state = 2;
    }
}

Texture TextureManager::upload(const std::string& path, unsigned char* data, int width, int height, int channels) {
    GLuint texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    GLenum format = GL_RGB;
    if (channels == 4) format = GL_RGBA;
#ifdef WESTON_PLATFORM
    else if (channels == 1) format = GL_LUMINANCE;
#else
    else if (channels == 1) format = GL_RED;
#endif
    // rows of 3 bytes are not 4-byte aligned
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // The box of the picture keeps the natural size from its header; the GPU gets the size the picture was reduced to.
    int natural_width = width, natural_height = height;
    int header_width = 0, header_height = 0;
    if (image_size(path, header_width, header_height)) {
        natural_width = header_width;
        natural_height = header_height;
    }

    Texture tex = {texture_id, natural_width, natural_height, width, height};
    textures[path] = tex;
    Usage entry;
    entry.frame = frame_;
    entry.bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    if (!is_http_url(path) && !memory_files::find(path)) {
        // the time the file was written: get_texture() compares it again after a second (see file_changed)
        std::error_code error;
        entry.modified = std::filesystem::last_write_time(path, error);
        entry.checked = std::chrono::steady_clock::now();
    }
    usage[path] = entry;
    log_texture(path, natural_width, natural_height, width, height);
    return tex;
}

// The configured limit, or the driver limit when it is 0: called before the texture is created, so GL is ready
int TextureManager::effective_max_side() {
    if (max_side_ > 0) return max_side_;
    if (driver_max_side_ == 0) {
        GLint limit = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
        driver_max_side_ = limit > 0 ? static_cast<int>(limit) : 2048;
        if (driver_max_side_ > 8192) driver_max_side_ = 8192;
    }
    return driver_max_side_;
}

// A picture larger than the limit is reduced here, before it reaches the GPU; the layout keeps the natural size
bool TextureManager::shrink_to_side(std::vector<unsigned char>& pixels, int& w, int& h, int side) {
    if (side <= 0) return false;
    const int longest = std::max(w, h);
    if (longest <= side) return false;
    const double scale = static_cast<double>(side) / longest;
    const int new_w = std::max(1, static_cast<int>(std::lround(w * scale)));
    const int new_h = std::max(1, static_cast<int>(std::lround(h * scale)));
    std::vector<unsigned char> resized(static_cast<size_t>(new_w) * static_cast<size_t>(new_h) * 4u);
    if (!stbir_resize_uint8(pixels.data(), w, h, w * 4, resized.data(), new_w, new_h, new_w * 4, 4)) return false;
    pixels.swap(resized);
    w = new_w;
    h = new_h;
    return true;
}

void TextureManager::set_max_side(int side) {
    max_side_ = side;
}

void TextureManager::set_budget_mb(size_t megabytes) {
    if (megabytes == 0) return;             // 0 keeps the default
    budget_bytes_ = megabytes * 1024u * 1024u;
}

// The cache is by path, so a file written again under the same name would keep showing the old picture. The time of
// the file is read again at most once a second; when it moved, the stale texture is freed here (the GL thread) and the
// caller loads the picture anew.
bool TextureManager::file_changed(const std::string& path) {
    if (is_http_url(path) || memory_files::find(path)) return false;    // a downloaded file does not change under its name
    auto entry = usage.find(path);
    if (entry == usage.end()) return false;
    const auto now = std::chrono::steady_clock::now();
    if (now - entry->second.checked < std::chrono::seconds(1)) return false;
    entry->second.checked = now;
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error) return false;
    if (modified == entry->second.modified) return false;

    auto texture = textures.find(path);
    if (texture != textures.end()) {
        glDeleteTextures(1, &texture->second.id);
        textures.erase(texture);
    }
    usage.erase(entry);
    sizes.erase(path);                      // the header of the new file may give another size
    return true;
}

Texture TextureManager::get_texture(const std::string& path) {
    auto it = textures.find(path);
    if (it != textures.end() && !file_changed(path)) {
        usage[path].frame = frame_;
        return it->second;
    }
    if (failed.count(path)) return {0, 0, 0};
    auto job_it = jobs.find(path);
    if (job_it != jobs.end()) return {0, 0, 0};          // still decoding

    // A picture on the network always goes to the worker, even a small one: the request would hold the draw thread for
    // as long as the server takes. A small file is decoded here: it is quick, and an icon must not appear a frame late.
    if (!media::is_url(path) && file_size(path) < kAsyncBytes) {
        int width, height, channels;
        stbi_set_flip_vertically_on_load(false);
        unsigned char* data = load_picture(path, width, height, channels);
        if (!data) {
            std::cerr << "Failed to load texture: " << path << " Error: " << stbi_failure_reason() << std::endl;
            failed.insert(path);
            return {0, 0, 0};
        }
        std::vector<unsigned char> pixels(data, data + static_cast<size_t>(width) * static_cast<size_t>(height) * channels);
        stbi_image_free(data);
        const int side = effective_max_side();
        if (std::max(width, height) > side) {
            if (channels != 4) {
                widen_to_rgba(pixels, width, height, channels);
                channels = 4;
            }
            shrink_to_side(pixels, width, height, side);
        }
        return upload(path, pixels.data(), width, height, channels);
    }

    // a large picture, or one on the network: the worker decodes it, upload_pending() makes the texture when it is ready
    auto job = std::make_shared<Job>();
    job->path = path;
    job->side = effective_max_side();   // resolved here: the worker thread has no GL context
    jobs[path] = job;
    ++in_flight_;
    start_worker();
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.push_back(job);
    }
    queue_cv_.notify_one();
    return {0, 0, 0};
}

bool TextureManager::image_size(const std::string& path, int& width, int& height) {
    auto it = textures.find(path);
    if (it != textures.end()) {
        width = it->second.width;
        height = it->second.height;
        return true;
    }
    if (media::is_url(path)) {
        // the size of a picture on the network is known only once the worker has decoded it
        return known_url_size(path, width, height);
    }
    auto known = sizes.find(path);
    if (known == sizes.end()) {
        int w = 0, h = 0, channels = 0;
        const auto bytes = memory_files::find(path);
        const bool known_size = bytes ? stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(bytes->data()), static_cast<int>(bytes->size()), &w, &h, &channels)
                                      : stbi_info(path.c_str(), &w, &h, &channels);
        if (!known_size) w = h = 0;
        known = sizes.emplace(path, std::make_pair(w, h)).first;
    }
    width = known->second.first;
    height = known->second.second;
    return width > 0 && height > 0;
}

bool TextureManager::upload_pending(double budget_ms) {
    ++frame_;
    bool arrived = false;
    if (!jobs.empty()) {
        const auto start = std::chrono::steady_clock::now();
        for (auto it = jobs.begin(); it != jobs.end();) {
            const int state = it->second->state.load();
            if (state == 2) {
                Job& job = *it->second;
                upload(it->first, job.pixels.data(), job.width, job.height, job.channels);
                // A picture from the network had no size while the page was laid out: its box takes the natural size
                // now, so the layout runs again (the boxes kept from the earlier pass are dropped).
                if (media::is_url(it->first)) {
                    ++g_layout_epoch;
                    g_dom_dirty = true;
                }
                arrived = true;
                --in_flight_;
                it = jobs.erase(it);
                // one big upload per frame is enough: the rest wait for the next one
                if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() >= budget_ms) break;
            } else if (state == 3) {
                // a picture from the network reported its error on the worker thread, where it failed
                if (!media::is_url(it->first))
                    std::cerr << "Failed to load texture: " << it->first << " Error: " << stbi_failure_reason() << std::endl;
                failed.insert(it->first);
                --in_flight_;
                it = jobs.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (arrived) evict_if_needed();
    return arrived;
}

// Large pictures that have not been drawn for a while go first; the ones drawn in the last frames stay
void TextureManager::evict_if_needed() {
    size_t total = 0;
    for (const auto& entry : usage) total += entry.second.bytes;
    while (total > budget_bytes_) {
        const std::string* oldest = nullptr;
        uint64_t oldest_frame = frame_;
        for (const auto& entry : usage) {
            if (entry.second.bytes < kLargeTextureBytes || entry.second.frame + 2 > frame_) continue;
            if (!oldest || entry.second.frame < oldest_frame) {
                oldest = &entry.first;
                oldest_frame = entry.second.frame;
            }
        }
        if (!oldest) break;
        const std::string path = *oldest;
        auto texture = textures.find(path);
        if (texture != textures.end()) {
            glDeleteTextures(1, &texture->second.id);
            textures.erase(texture);
        }
        total -= usage[path].bytes;
        usage.erase(path);
    }
}

Texture TextureManager::get_svg_texture(const std::string& path, int width, int height, const std::string& color) {
    std::string key = path + "?" + std::to_string(width) + "x" + std::to_string(height) + "&" + color;
    auto it = textures.find(key);
    if (it != textures.end()) {
        return it->second;
    }

    Texture tex = SvgLoader::load_svg(path, width, height, color);
    tex.gpu_width = tex.width;
    tex.gpu_height = tex.height;
    if (tex.id != 0) {
        textures[key] = tex;
        log_texture(path, tex.width, tex.height, tex.gpu_width, tex.gpu_height);
    }
    return tex;
}

Texture TextureManager::get_inline_svg_texture(const std::string& content, int width, int height, const std::string& color) {
    std::size_t content_hash = std::hash<std::string>{}(content);
    std::string key = "inline_svg_" + std::to_string(content_hash) + "?" + std::to_string(width) + "x" + std::to_string(height) + "&" + color;
    
    auto it = textures.find(key);
    if (it != textures.end()) {
        return it->second;
    }

    Texture tex = SvgLoader::load_svg_from_string(content, width, height, color);
    tex.gpu_width = tex.width;
    tex.gpu_height = tex.height;
    if (tex.id != 0) {
        textures[key] = tex;
        log_texture(key, tex.width, tex.height, tex.gpu_width, tex.gpu_height);
    }
    return tex;
}

void TextureManager::clear() {
    for (auto& pair : textures) {
        glDeleteTextures(1, &pair.second.id);
    }
    textures.clear();
    usage.clear();
    jobs.clear();        // a picture the worker is decoding is dropped when it ends
    failed.clear();
    sizes.clear();
    in_flight_ = 0;
    SvgLoader::clear_cache();
}

} // namespace linweb
