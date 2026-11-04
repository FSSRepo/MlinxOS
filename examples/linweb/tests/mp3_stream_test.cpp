// Decodes an MP3 with Mp3Stream and writes raw S16_LE PCM, checking it streams frame by frame.
//   g++ -std=c++17 -Isrc/media/mp3 -Iexternal/openmp3 tests/mp3_stream_test.cpp src/media/mp3/mp3_stream.cpp \
//       src/media/mp3/decode.cpp external/openmp3/*.cpp -o /tmp/mp3_stream_test && /tmp/mp3_stream_test song.mp3 [out.raw]
#include "mp3_stream.h"

#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: %s file.mp3 [out.raw]\n", argv[0]); return 2; }
    linweb::Mp3Stream stream;
    if (!stream.open(argv[1])) { std::printf("FAIL: cannot open %s\n", argv[1]); return 1; }
    std::printf("rate=%d channels=%d bitrate=%d\n", stream.sample_rate(), stream.channels(), stream.bitrate());
    std::FILE* out = argc > 2 ? std::fopen(argv[2], "wb") : nullptr;
    std::vector<int16_t> pcm(static_cast<size_t>(linweb::Mp3Stream::kFrameSamples) * stream.channels());
    long frames = 0;
    int peak = 0;
    while (stream.read_frame(pcm.data())) {
        ++frames;
        for (int16_t v : pcm) { int a = v < 0 ? -v : v; if (a > peak) peak = a; }
        if (out) std::fwrite(pcm.data(), sizeof(int16_t), pcm.size(), out);
    }
    if (out) std::fclose(out);
    double seconds = static_cast<double>(frames) * linweb::Mp3Stream::kFrameSamples / stream.sample_rate();
    std::printf("frames=%ld seconds=%.1f peak=%d\n", frames, seconds, peak);
    if (frames < 10 || peak < 100) { std::printf("FAIL: little or silent audio\n"); return 1; }
    std::printf("OK\n");
    return 0;
}
