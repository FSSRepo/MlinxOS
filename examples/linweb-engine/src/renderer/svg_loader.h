#pragma once
#include <string>
#include <vector>
#include <memory>
#include "gl_wrapper.h"
#include "renderer/texture_manager.h"

namespace linweb {

struct Node; // Forward declaration

class SvgLoader {
public:
    static std::string reconstruct_svg(const std::shared_ptr<linweb::Node>& node);

    static Texture load_svg(const std::string& path, int width, int height, const std::string& color);

    static Texture load_svg_from_string(const std::string& content, int width, int height, const std::string& color);

    static bool get_intrinsic_size_from_string(const std::string& content, float& width, float& height);

    static bool get_intrinsic_size(const std::string& path, float& width, float& height);

    static void clear_cache();

private:
    static std::string preprocess(const std::string& content, int width, int height, const std::string& color);
    
    struct IntrinsicSize {
        float width;
        float height;
    };
    static std::unordered_map<std::string, IntrinsicSize> size_cache;

    struct CachedReconstruction {
        std::string content;
        size_t attr_hash;
    };
    static std::unordered_map<Node*, CachedReconstruction> reconstruction_cache;

    static size_t hash_node_state(const std::shared_ptr<linweb::Node>& node);
};

} // namespace linweb
