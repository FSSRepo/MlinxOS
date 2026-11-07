#include "renderer/background_renderer.h"
#include "renderer/renderer.h"
#include "renderer/renderer_internal.h"
#include "renderer/texture_manager.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace linweb {
namespace {
// The bytes of a "data:" URI: percent-encoded text or base64
std::string decode_data_uri(const std::string& uri, std::string& mime) {
    size_t comma = uri.find(',');
    if (comma == std::string::npos) return "";
    std::string header = uri.substr(5, comma - 5);
    mime = header.substr(0, header.find(';'));
    bool base64 = header.find(";base64") != std::string::npos;
    std::string body = uri.substr(comma + 1), out;
    if (base64) {
        static const std::string table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        unsigned bits = 0; int count = 0;
        for (char c : body) {
            size_t v = table.find(c);
            if (v == std::string::npos) continue;
            bits = (bits << 6) | static_cast<unsigned>(v);
            count += 6;
            if (count >= 8) { count -= 8; out += static_cast<char>((bits >> count) & 0xFF); }
        }
        return out;
    }
    for (size_t i = 0; i < body.size(); ++i) {
        if (body[i] == '%' && i + 2 < body.size() + 0 && std::isxdigit(static_cast<unsigned char>(body[i + 1])) && std::isxdigit(static_cast<unsigned char>(body[i + 2]))) {
            out += static_cast<char>(std::stoi(body.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else out += body[i];
    }
    return out;
}
}

void draw_background_image(const LayoutBox& box, const Rect& area, float opacity) {
    std::string value = box.styled_node->value("background-image");
    if (value.empty()) value = box.styled_node->value("background");
    size_t begin = value.find("url(");
    if (begin == std::string::npos || area.width <= 0 || area.height <= 0) return;
    begin += 4;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    if (begin == value.size()) return;
    char quote = value[begin] == '\'' || value[begin] == '"' ? value[begin++] : 0;
    size_t end = value.find(quote ? quote : ')', begin);
    if (end == std::string::npos) return;
    std::string path = value.substr(begin, end - begin);
    const bool is_data = path.compare(0, 5, "data:") == 0;
    if (path.empty() || (!is_data && path.find("://") != std::string::npos)) return;
    Texture texture;
    if (is_data) {
        std::string mime;
        std::string bytes = decode_data_uri(path, mime);
        if (mime != "image/svg+xml" || bytes.empty()) return;   // other data images are not read
        // rasterised at the size that covers the area with the picture's own proportions (its width and height)
        float natural_w = area.width, natural_h = area.height;
        size_t tag_end = bytes.find('>');
        std::string tag = bytes.substr(0, tag_end);
        auto attribute = [&](const char* name, float& out) {
            size_t at = tag.find(std::string(" ") + name + "=\"");
            if (at == std::string::npos) return;
            try { out = std::stof(tag.substr(at + std::string(name).size() + 3)); } catch (...) {}
        };
        attribute("width", natural_w);
        attribute("height", natural_h);
        // without a viewBox the rasteriser would crop the picture to the new size instead of scaling it
        if (tag.find("viewBox") == std::string::npos && bytes.compare(0, 4, "<svg") == 0)
            bytes.insert(4, " viewBox=\"0 0 " + std::to_string(natural_w) + " " + std::to_string(natural_h) + "\"");
        float cover = std::max(area.width / natural_w, area.height / natural_h);
        texture = TextureManager::get().get_inline_svg_texture(bytes, static_cast<int>(natural_w * cover), static_cast<int>(natural_h * cover), box.styled_node->value("color"));
    } else if (path.size() >= 4 && path.substr(path.size() - 4) == ".svg")
        texture = TextureManager::get().get_svg_texture(path, static_cast<int>(area.width), static_cast<int>(area.height), box.styled_node->value("color"));
    else texture = TextureManager::get().get_texture(path);
    if (!texture.id || texture.width <= 0 || texture.height <= 0) return;
    float width = static_cast<float>(texture.width), height = static_cast<float>(texture.height);
    std::string size = box.styled_node->value("background-size");
    // the size in the shorthand follows a slash: "center / cover no-repeat"
    if (size.empty()) {
        size_t slash = value.find(')', end);
        size_t at = slash == std::string::npos ? std::string::npos : value.find('/', slash);
        if (at != std::string::npos) {
            std::istringstream after(value.substr(at + 1));
            std::string token;
            if (after >> token && token != "no-repeat" && token != "repeat") size = token;
        }
    }
    if (size == "cover" || size == "contain") {
        float scale = size == "cover" ? std::max(area.width / width, area.height / height) : std::min(area.width / width, area.height / height);
        width *= scale; height *= scale;
    } else if (!size.empty() && size != "auto") {
        std::istringstream parts(size);
        std::string first, second;
        parts >> first >> second;
        if (first != "auto") {
            width = LayoutEngine::parse_px(first, area.width);
            height *= width / texture.width;
        }
        if (!second.empty() && second != "auto") {
            height = LayoutEngine::parse_px(second, area.height);
            if (first == "auto") width = height * texture.width / texture.height;
        }
    }
    if (width < 0.1f || height < 0.1f) return;
    std::string position = box.styled_node->value("background-position");
    std::string repeat = box.styled_node->value("background-repeat");
    // Common legacy shorthand: url(...) x y repeat-x/no-repeat.
    if (position.empty()) {
        size_t close = value.find(')', end);
        if (!quote) close = end;
        if (close != std::string::npos) {
            std::istringstream tail(value.substr(close + 1));
            std::string token;
            while (tail >> token) {
                if (token == "repeat" || token == "repeat-x" || token == "repeat-y" || token == "no-repeat") { if (repeat.empty()) repeat = token; }
                else if (token == "center" || token == "left" || token == "right" || token == "top" || token == "bottom" || token[0] == '-' || token[0] == '.' || std::isdigit(static_cast<unsigned char>(token[0]))) {
                    if (!position.empty()) position += ' ';
                    position += token;
                }
            }
        }
    }
    std::istringstream positions(position);
    std::string horizontal = "0", vertical = "0";
    positions >> horizontal;
    if (!(positions >> vertical)) vertical = horizontal == "center" ? "center" : "0";
    if (horizontal == "top" || horizontal == "bottom") { std::swap(horizontal, vertical); if (horizontal == "0") horizontal = "center"; }
    auto offset = [](const std::string& token, float extra) {
        if (token == "center") return extra / 2;
        if (token == "right" || token == "bottom") return extra;
        if (token == "left" || token == "top") return 0.0f;
        return LayoutEngine::parse_px(token, extra);
    };
    float x = area.x + offset(horizontal, area.width - width), y = area.y + offset(vertical, area.height - height);
    bool repeat_x = repeat.empty() || repeat == "repeat" || repeat == "repeat-x";
    bool repeat_y = repeat.empty() || repeat == "repeat" || repeat == "repeat-y";
    if (repeat_x) x = area.x + std::fmod(x - area.x, width) - width;
    if (repeat_y) y = area.y + std::fmod(y - area.y, height) - height;
    GLint previous[4] = {0, 0, RendererState::viewport_width, RendererState::viewport_height};
    GLboolean enabled = glIsEnabled(GL_SCISSOR_TEST);
    if (enabled) glGetIntegerv(GL_SCISSOR_BOX, previous);
    int left = std::max(previous[0], static_cast<int>(std::floor(area.x)));
    int bottom = std::max(previous[1], RendererState::viewport_height - static_cast<int>(std::ceil(area.y + area.height)));
    int right = std::min(previous[0] + previous[2], static_cast<int>(std::ceil(area.x + area.width)));
    int top = std::min(previous[1] + previous[3], RendererState::viewport_height - static_cast<int>(std::floor(area.y)));
    glEnable(GL_SCISSOR_TEST);
    glScissor(left, bottom, std::max(0, right - left), std::max(0, top - bottom));
    unsigned tiles = 0;
    for (float ty = y; ty < area.y + area.height; ty += height) {
        for (float tx = x; tx < area.x + area.width; tx += width) {
            if (++tiles > 16384) break;
            Renderer::draw_image(texture.id, tx, ty, width, height, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, opacity);
            if (!repeat_x) break;
        }
        if (!repeat_y || tiles > 16384) break;
    }
    if (enabled) glScissor(previous[0], previous[1], previous[2], previous[3]);
    else glDisable(GL_SCISSOR_TEST);
}
}
