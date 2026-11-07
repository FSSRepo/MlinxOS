#include "debug/frame_capture.h"
#include "gl_wrapper.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include "layout/layout_engine.h"
#include <functional>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace linweb {
bool dump_layout(const std::string& path, const LayoutBox& root) {
    std::ofstream out(path);
    if (!out) return false;
    out << "depth\ttag\tid\tx\ty\twidth\theight\tfont-size\tdisplay\talign\tmin-width\ttotal-height\n";
    std::function<void(const LayoutBox&, int)> visit = [&](const LayoutBox& box, int depth) {
        const auto& s = box.styled_node;
        out << depth << '\t' << s->node->tag_name << '\t' << s->node->data.id() << '\t'
            << box.dimensions.content.x << '\t' << box.dimensions.content.y << '\t'
            << box.dimensions.content.width << '\t' << box.dimensions.content.height << '\t'
            << s->value("font-size") << '\t' << s->value("display") << '\t' << s->value("text-align") << '\t' << s->value("min-width") << '\t' << box.total_content_height << '\n';
        for (const auto& child : box.children) visit(*child, depth + 1);
    };
    visit(root, 0);
    return out.good();
}
bool capture_frame(const std::string& path, int width, int height) {
    if (width <= 0 || height <= 0) return false;
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::string extension = path.size() >= 4 ? path.substr(path.size() - 4) : std::string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (extension == ".png") {
        stbi_flip_vertically_on_write(1);
        return stbi_write_png(path.c_str(), width, height, 3, pixels.data(), width * 3) != 0;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    uint32_t stride = (static_cast<uint32_t>(width) * 3 + 3) & ~3u;
    uint32_t bytes = stride * height;
    auto word = [&](uint32_t value, int count) {
        for (int i = 0; i < count; ++i) out.put(static_cast<char>(value >> (8 * i)));
    };
    out.write("BM", 2); word(54 + bytes, 4); word(0, 4); word(54, 4);
    word(40, 4); word(width, 4); word(height, 4); word(1, 2); word(24, 2);
    word(0, 4); word(bytes, 4); word(2835, 4); word(2835, 4); word(0, 4); word(0, 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t index = (static_cast<size_t>(y) * width + x) * 3;
            out.put(pixels[index + 2]); out.put(pixels[index + 1]); out.put(pixels[index]);
        }
        for (uint32_t pad = width * 3; pad < stride; ++pad) out.put(0);
    }
    return out.good();
}
}
