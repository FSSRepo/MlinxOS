#pragma once
#include <string>
#include "renderer/renderer.h"
#include "renderer/texture_manager.h"

namespace linweb {

class SvgRenderer {
public:
    static std::string parse_svg(const std::shared_ptr<Node>& node);
    static Texture rasterize_svg(const std::string& content, int width, int height, const std::string& color);
    static void draw_svg(const std::string& content, float x, float y, float width, float height, const std::string& color = "black", float radius = 0.0f);
};

} // namespace linweb
