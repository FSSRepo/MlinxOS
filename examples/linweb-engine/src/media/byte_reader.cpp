#include "media/byte_reader.h"
#include "network/http_client.h"

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <list>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace linweb {
namespace media {

namespace {

// ---- a file ------------------------------------------------------------------------------------------------------
class FileReader : public ByteReader {
public:
    explicit FileReader(const std::string& path) {
        file_ = fopen(path.c_str(), "rb");
        if (!file_) return;
        fseek(file_, 0, SEEK_END);
        size_ = static_cast<int64_t>(ftell(file_));
        fseek(file_, 0, SEEK_SET);
    }
    ~FileReader() override { if (file_) fclose(file_); }
    bool ok() const { return file_ != nullptr; }
    int64_t size() const override { return size_; }
    size_t read(void* data, size_t length) override {
        const size_t n = fread(data, 1, length, file_);
        position_ += static_cast<int64_t>(n);
        return n;
    }
    bool seek(int64_t position) override {
        if (fseek(file_, static_cast<long>(position), SEEK_SET) != 0) return false;
        position_ = position;
        return true;
    }
    int64_t tell() const override { return position_; }

private:
    FILE* file_ = nullptr;
    int64_t size_ = 0;
    int64_t position_ = 0;
};

// ---- an http(s) URL ----------------------------------------------------------------------------------------------
constexpr int64_t kBlock = 1 << 20;     // bytes asked for with each request
constexpr int kReadAhead = 3;           // blocks fetched in advance of the one being read
constexpr size_t kCachedBlocks = 48;    // blocks kept (48 MiB)

class HttpReader : public ByteReader {
public:
    // Learns the size of the file with the first block; throws std::runtime_error when the server cannot be used.
    explicit HttpReader(const std::string& url) : url_(url) {
        HttpClient client;
        HttpResponse first = client.get(url_, {{"Range", "bytes=0-" + std::to_string(kBlock - 1)}});
        url_ = first.url;   // after the redirects
        if (first.status == 206) {
            const auto range = first.headers.find("content-range");   // "bytes 0-1048575/150000000"
            const size_t slash = range == first.headers.end() ? std::string::npos : range->second.rfind('/');
            if (slash == std::string::npos) throw std::runtime_error("the server did not tell the size of the file");
            total_ = std::atoll(range->second.c_str() + slash + 1);
        } else {
            // the server ignores Range: the answer is the whole file (the client accepts up to 64 MiB of body)
            total_ = static_cast<int64_t>(first.body.size());
            whole_file_ = true;
        }
        if (total_ <= 0) throw std::runtime_error("the file is empty");
        store(0, std::make_shared<std::vector<uint8_t>>(first.body.begin(), first.body.end()));
        if (!whole_file_) worker_ = std::thread([this] { work(); });
    }

    ~HttpReader() override {
        {
            std::lock_guard<std::mutex> lock(m_);
            stop_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    int64_t size() const override { return total_; }
    int64_t tell() const override { return position_; }
    bool seek(int64_t position) override {
        if (position < 0) return false;
        position_ = position;
        return true;
    }
    std::string error() const override {
        std::lock_guard<std::mutex> lock(m_);
        return error_;
    }

    size_t read(void* data, size_t length) override {
        uint8_t* out = static_cast<uint8_t*>(data);
        size_t done = 0;
        while (done < length && position_ < total_) {
            const int64_t block = position_ / kBlock;
            std::shared_ptr<std::vector<uint8_t>> bytes = wait_for(block);
            if (!bytes) break;
            const size_t offset = static_cast<size_t>(position_ - block * kBlock);
            if (offset >= bytes->size()) break;
            const size_t n = std::min(length - done, bytes->size() - offset);
            std::memcpy(out + done, bytes->data() + offset, n);
            done += n;
            position_ += static_cast<int64_t>(n);
        }
        return done;
    }

private:
    void store(int64_t block, std::shared_ptr<std::vector<uint8_t>> bytes) {
        std::lock_guard<std::mutex> lock(m_);
        cache_[block] = std::move(bytes);
        lru_.remove(block);
        lru_.push_back(block);
        while (lru_.size() > kCachedBlocks) {
            const int64_t old = lru_.front();
            lru_.pop_front();
            cache_.erase(old);
        }
    }

    // The block, waiting for the worker to fetch it; null when the download failed or the reader is closing.
    std::shared_ptr<std::vector<uint8_t>> wait_for(int64_t block) {
        std::unique_lock<std::mutex> lock(m_);
        const int64_t blocks = (total_ + kBlock - 1) / kBlock;
        for (;;) {
            auto found = cache_.find(block);
            if (found != cache_.end()) {
                lru_.remove(block);
                lru_.push_back(block);
                std::shared_ptr<std::vector<uint8_t>> bytes = found->second;
                // keep the next blocks coming
                if (!whole_file_) {
                    for (int ahead = 1; ahead <= kReadAhead && block + ahead < blocks; ++ahead) request_locked(block + ahead, false);
                    cv_.notify_all();
                }
                return bytes;
            }
            if (stop_ || failed_ || whole_file_) return nullptr;
            request_locked(block, true);
            cv_.notify_all();
            cv_.wait(lock);
        }
    }

    // queue a block for the worker; a block that is wanted now goes first
    void request_locked(int64_t block, bool urgent) {
        if (cache_.count(block) || fetching_ == block) return;
        auto at = std::find(queue_.begin(), queue_.end(), block);
        if (at != queue_.end()) {
            if (!urgent) return;
            queue_.erase(at);
        }
        if (urgent) queue_.push_front(block); else queue_.push_back(block);
    }

    void work() {
        std::unique_ptr<HttpClient> client;
        for (;;) {
            int64_t block;
            {
                std::unique_lock<std::mutex> lock(m_);
                cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
                if (stop_) return;
                block = queue_.front();
                queue_.pop_front();
                if (cache_.count(block)) continue;
                fetching_ = block;
            }
            try {
                if (!client) client.reset(new HttpClient);
                const int64_t first = block * kBlock;
                const int64_t last = std::min(total_, first + kBlock) - 1;
                HttpResponse response = client->get(url_, {{"Range", "bytes=" + std::to_string(first) + "-" + std::to_string(last)}});
                if (response.status != 206) throw std::runtime_error("the server stopped answering Range requests");
                store(block, std::make_shared<std::vector<uint8_t>>(response.body.begin(), response.body.end()));
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(m_);
                failed_ = true;
                error_ = e.what();
            }
            {
                std::lock_guard<std::mutex> lock(m_);
                fetching_ = -1;
            }
            cv_.notify_all();
        }
    }

    std::string url_;
    int64_t total_ = 0;
    int64_t position_ = 0;     // only the thread that reads touches it
    bool whole_file_ = false;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::map<int64_t, std::shared_ptr<std::vector<uint8_t>>> cache_;
    std::list<int64_t> lru_;
    std::deque<int64_t> queue_;
    int64_t fetching_ = -1;
    bool stop_ = false;
    bool failed_ = false;
    std::string error_;
    std::thread worker_;
};

} // namespace

bool is_url(const std::string& path) {
    return path.compare(0, 7, "http://") == 0 || path.compare(0, 8, "https://") == 0;
}

std::unique_ptr<ByteReader> open_reader(const std::string& path_or_url, std::string& error) {
    if (is_url(path_or_url)) {
        try {
            return std::unique_ptr<ByteReader>(new HttpReader(path_or_url));
        } catch (const std::exception& e) {
            error = e.what();
            return nullptr;
        }
    }
    std::unique_ptr<FileReader> reader(new FileReader(path_or_url));
    if (!reader->ok()) {
        error = "cannot open the file";
        return nullptr;
    }
    return std::unique_ptr<ByteReader>(reader.release());
}

} // namespace media
} // namespace linweb
