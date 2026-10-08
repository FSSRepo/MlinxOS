#include "renderer/svg_renderer.h"
#include "renderer/renderer.h"
#include "renderer/texture_manager.h"
#include "renderer/svg_loader.h"

namespace linweb {

std::string SvgRenderer::parse_svg(const std::shared_ptr<Node>& node) {
    return SvgLoader::reconstruct_svg(node);
}

Texture SvgRenderer::rasterize_svg(const std::string& content, int width, int height, const std::string& color) {
    return SvgLoader::load_svg_from_string(content, width, height, color);
}

void SvgRenderer::draw_svg(const std::string& content, float x, float y, float width, float height, const std::string& color, float radius) {
    Texture tex = SvgLoader::load_svg_from_string(content, static_cast<int>(width), static_cast<int>(height), color);
    if (tex.id != 0) {
        Renderer::draw_image(tex.id, x, y, width, height, radius);
    }
}

} // namespace linweb
