#pragma once
#include <string>
#include <unordered_map>
#include "gl_wrapper.h"

namespace linweb {

struct Texture {
    GLuint id = 0;
    int width = 0;
    int height = 0;
};

class TextureManager {
public:
    static TextureManager& get() {
        static TextureManager instance;
        return instance;
    }

    Texture get_texture(const std::string& path);
    Texture get_svg_texture(const std::string& path, int width, int height, const std::string& color);
    Texture get_inline_svg_texture(const std::string& content, int width, int height, const std::string& color);
    void clear();

private:
    TextureManager() = default;
    ~TextureManager();

    std::unordered_map<std::string, Texture> textures;
};

} // namespace linweb
