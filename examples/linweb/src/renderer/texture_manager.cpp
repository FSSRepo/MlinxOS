#include "renderer/texture_manager.h"
#include "renderer/svg_loader.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <iostream>

namespace linweb {

TextureManager::~TextureManager() {
    clear();
}

Texture TextureManager::get_texture(const std::string& path) {
    auto it = textures.find(path);
    if (it != textures.end()) {
        return it->second;
    }

    int width, height, channels;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 0);

    if (!data) {
        std::cerr << "Failed to load texture: " << path << " Error: " << stbi_failure_reason() << std::endl;
        return {0, 0, 0};
    }

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


    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(data);

    Texture tex = {texture_id, width, height};
    textures[path] = tex;
    return tex;
}

Texture TextureManager::get_svg_texture(const std::string& path, int width, int height, const std::string& color) {
    std::string key = path + "?" + std::to_string(width) + "x" + std::to_string(height) + "&" + color;
    auto it = textures.find(key);
    if (it != textures.end()) {
        return it->second;
    }

    Texture tex = SvgLoader::load_svg(path, width, height, color);
    if (tex.id != 0) {
        textures[key] = tex;
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
    if (tex.id != 0) {
        textures[key] = tex;
    }
    return tex;
}

void TextureManager::clear() {
    for (auto& pair : textures) {
        glDeleteTextures(1, &pair.second.id);
    }
    textures.clear();
    SvgLoader::clear_cache();
}

} // namespace linweb
