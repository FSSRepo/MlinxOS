#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace linweb {
namespace media {

// Where the bytes of a media file come from: a file on the disk, or an http(s) URL read in blocks with Range requests,
// so a video or a song plays while it downloads and seeks to any place without fetching what lies before it.
class ByteReader {
public:
    virtual ~ByteReader() {}
    virtual int64_t size() const = 0;
    virtual size_t read(void* data, size_t length) = 0;   // from the current position; fewer bytes only at the end or on failure
    virtual bool seek(int64_t position) = 0;              // absolute
    virtual int64_t tell() const = 0;
    virtual std::string error() const { return std::string(); }
};

bool is_url(const std::string& path);

// A file path or an http(s) URL; null (and `error`) when it cannot be opened.
std::unique_ptr<ByteReader> open_reader(const std::string& path_or_url, std::string& error);

} // namespace media
} // namespace linweb
