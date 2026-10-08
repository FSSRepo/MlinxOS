#include "renderer/svg_loader.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>
#include "core/dom.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

namespace linweb
{

    std::unordered_map<std::string, SvgLoader::IntrinsicSize> SvgLoader::size_cache;
    std::unordered_map<Node *, SvgLoader::CachedReconstruction> SvgLoader::reconstruction_cache;

    void SvgLoader::clear_cache()
    {
        size_cache.clear();
        reconstruction_cache.clear();
    }

    size_t SvgLoader::hash_node_state(const std::shared_ptr<linweb::Node> &node)
    {
        size_t h = 0;
        for (const auto &attr : node->data.attributes)
        {
            h ^= std::hash<std::string>{}(attr.first) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<std::string>{}(attr.second) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        auto text_children = node->get_text_children();
        if (!text_children.empty())
        {
            auto text_node = std::static_pointer_cast<linweb::TextNode>(text_children[0]);
            h ^= std::hash<std::string>{}(text_node->data) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }

    std::string SvgLoader::reconstruct_svg(const std::shared_ptr<linweb::Node> &node)
    {
        if (!node || node->tag_name != "svg")
            return "";

        size_t current_hash = hash_node_state(node);
        auto it = reconstruction_cache.find(node.get());
        if (it != reconstruction_cache.end() && it->second.attr_hash == current_hash)
        {
            return it->second.content;
        }

        std::stringstream ss;
        ss << "<svg";

        for (const auto &attr : node->data.attributes)
        {
            ss << " " << attr.first << "=\"" << attr.second << "\"";
        }
        ss << ">";

        auto text_children = node->get_text_children();
        if (!text_children.empty())
        {
            auto text_node = std::static_pointer_cast<linweb::TextNode>(text_children[0]);
            ss << text_node->data;
        }

        ss << "</svg>";
        std::string result = ss.str();
        reconstruction_cache[node.get()] = {result, current_hash};
        return result;
    }

    Texture SvgLoader::load_svg(const std::string &path, int width, int height, const std::string &color)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            std::cerr << "Failed to open SVG file: " << path << std::endl;
            return {0, 0, 0};
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        std::string processed = preprocess(content, width, height, color);

        NSVGimage *image = nsvgParse(processed.data(), "px", 96.0f);
        if (!image)
        {
            std::cerr << "Failed to parse SVG: " << path << std::endl;
            return {0, 0, 0};
        }

        NSVGrasterizer *rast = nsvgCreateRasterizer();
        if (!rast)
        {
            std::cerr << "Failed to create SVG rasterizer" << std::endl;
            nsvgDelete(image);
            return {0, 0, 0};
        }

        unsigned char *img_data = (unsigned char *)malloc(width * height * 4);
        if (!img_data)
        {
            std::cerr << "Failed to allocate memory for SVG rasterization" << std::endl;
            nsvgDeleteRasterizer(rast);
            nsvgDelete(image);
            return {0, 0, 0};
        }

        float scale = 1.0f;
        if (image->width > 0 && image->height > 0)
        {
            float scaleX = (float)width / image->width;
            float scaleY = (float)height / image->height;
            scale = (scaleX < scaleY) ? scaleX : scaleY;
        }

        nsvgRasterize(rast, image, 0, 0, scale, img_data, width, height, width * 4);

        GLuint texture_id;
        glGenTextures(1, &texture_id);
        glBindTexture(GL_TEXTURE_2D, texture_id);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img_data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        free(img_data);
        nsvgDeleteRasterizer(rast);
        nsvgDelete(image);

        return {texture_id, width, height};
    }

    Texture SvgLoader::load_svg_from_string(const std::string &content, int width, int height, const std::string &color)
    {
        std::string processed = preprocess(content, width, height, color);

        NSVGimage *image = nsvgParse(processed.data(), "px", 96.0f);
        if (!image)
        {
            std::cerr << "Failed to parse inline SVG" << std::endl;
            return {0, 0, 0};
        }

        NSVGrasterizer *rast = nsvgCreateRasterizer();
        if (!rast)
        {
            nsvgDelete(image);
            return {0, 0, 0};
        }

        unsigned char *img_data = (unsigned char *)malloc(width * height * 4);
        if (!img_data)
        {
            nsvgDeleteRasterizer(rast);
            nsvgDelete(image);
            return {0, 0, 0};
        }

        float scale = 1.0f;
        if (image->width > 0 && image->height > 0)
        {
            float scaleX = (float)width / image->width;
            float scaleY = (float)height / image->height;
            scale = (scaleX < scaleY) ? scaleX : scaleY;
        }

        nsvgRasterize(rast, image, 0, 0, scale, img_data, width, height, width * 4);

        GLuint texture_id;
        glGenTextures(1, &texture_id);
        glBindTexture(GL_TEXTURE_2D, texture_id);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img_data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        free(img_data);
        nsvgDeleteRasterizer(rast);
        nsvgDelete(image);

        return {texture_id, width, height};
    }

    bool SvgLoader::get_intrinsic_size_from_string(const std::string &content, float &width, float &height)
    {
        auto it = size_cache.find(content);
        if (it != size_cache.end())
        {
            width = it->second.width;
            height = it->second.height;
            return true;
        }

        std::vector<char> mutable_content(content.begin(), content.end());
        mutable_content.push_back('\0');

        NSVGimage *image = nsvgParse(mutable_content.data(), "px", 96.0f);
        if (!image)
            return false;

        width = image->width;
        height = image->height;

        size_cache[content] = {width, height};

        nsvgDelete(image);
        return true;
    }

    bool SvgLoader::get_intrinsic_size(const std::string &path, float &width, float &height)
    {
        auto it = size_cache.find(path);
        if (it != size_cache.end())
        {
            width = it->second.width;
            height = it->second.height;
            return true;
        }

        std::ifstream file(path);
        if (!file.is_open())
            return false;

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        if (get_intrinsic_size_from_string(content, width, height))
        {
            size_cache[path] = {width, height};
            return true;
        }
        return false;
    }

    std::string SvgLoader::preprocess(const std::string &content, int width, int height, const std::string &color)
    {
        std::string result = content;

        if (!color.empty()) {
            size_t pos = 0;
            while ((pos = result.find("currentColor", pos)) != std::string::npos) {
                result.replace(pos, 12, color);
                pos += color.length();
            }
        }

        size_t svg_start = result.find("<svg");
        if (svg_start != std::string::npos)
        {
            const size_t svg_end = result.find(">", svg_start);
            if (svg_end == std::string::npos)
            {
                return result;
            }
            
            std::string tag = result.substr(svg_start, svg_end - svg_start + 1);

           auto removeAttribute = [&](const std::string& name) {
                std::regex attr_regex("\\s+" + name + R"(\s*=\s*"[^"]*")");
                tag = std::regex_replace(tag, attr_regex, "");
            };

            removeAttribute("width");
            removeAttribute("height");

            if (tag.back() == '>') {
                tag.insert(tag.size() - 1,
                    " width=\"" + std::to_string(width) +
                    "\" height=\"" + std::to_string(height) + "\"");
            }

            result.replace(svg_start, svg_end - svg_start + 1, tag);
        }

        return result;
    }
} // namespace linweb
