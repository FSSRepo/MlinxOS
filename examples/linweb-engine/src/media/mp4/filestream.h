#ifndef __FILE_STREAM_H__
#define __FILE_STREAM_H__
#include <stdio.h>
#include <string>
#include "binary_utils.h"
#include "media/byte_reader.h"

// The bytes of an MP4 or an MP3 file come from a ByteReader: a file on the disk or an http(s) URL read with Range requests.
struct file_stream {
    linweb::media::ByteReader* file = nullptr;   // null when the source cannot be opened
    uint8_t* temp_buffer;
    int offset;
    int count = 0;
    int file_size = 0;

    file_stream(const char* path) {
        std::string error;
        file = linweb::media::open_reader(path, error).release();
        temp_buffer = new uint8_t[12];
        offset = 0;
        if (file) file_size = static_cast<int>(file->size());
    }

    ~file_stream() {
        delete file;
        delete[] temp_buffer;
    }

    void skip(int len) {
        if (file) file->seek(file->tell() + len);
        count += len;
    }

    void set(int pos) {
        if (file) file->seek(pos);
        count = pos;
    }

    bool end_of_file(int attempt_to_read) {
        return count + attempt_to_read >= file_size;
    }

    void read(uint8_t* data, int len) {
        if (file) file->read(data, static_cast<size_t>(len));
        count += len;
    }

    void flush(int len) {
        count = 0;
        read(len);
    }

    void read(int len) {
        if (file) file->read(temp_buffer, static_cast<size_t>(len));
        offset = 0;
        count += len;
    }

    uint8_t get() {
        uint8_t x = temp_buffer[offset];
        offset ++;
        return x;
    }

    const char* get_string(int len) {
        temp_buffer[offset + len] = '\0';
        const char* x = (const char*)temp_buffer + offset;
        offset += len;
        return x;
    }

    // big endian
    uint16_t next_uint16() {
        uint16_t x = read_uint16_be(temp_buffer + offset);
        offset += 2;
        return x;
    }

    uint32_t next_uint32() {
        uint32_t x = read_uint32_be(temp_buffer + offset);
        offset += 4;
        return x;
    }

    uint64_t next_uint64() {
        uint64_t x = read_uint64_be(temp_buffer + offset);
        offset += 8;
        return x;
    }
};
#endif