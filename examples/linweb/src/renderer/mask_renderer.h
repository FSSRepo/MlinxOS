#pragma once
#include <string>
#include <vector>
#include "gl_wrapper.h"

namespace linweb {

// One stop of a CSS mask-image linear gradient: the mask alpha at a distance along the gradient line (in px).
struct MaskStop {
    float position = 0.0f;
    float alpha = 1.0f;
};

// Reads "linear-gradient(to bottom, #000 0, #000 calc(100% - 48px), transparent 100%)" (and the "to top" form)
// for a box of height box_h. reversed is true for "to top". Returns false for anything else.
bool parse_mask_gradient(const std::string& value, float box_h, std::vector<MaskStop>& stops, bool& reversed);

// A mask layer: the children of a masked box are drawn into an offscreen target first. begin returns false when a
// layer is already open (nested masks draw without a layer); end restores the previous framebuffer.
bool mask_layer_begin(GLint& previous_framebuffer);
void mask_layer_end(GLint previous_framebuffer);

// Draws the layer over the box rectangle (x, y, w, h), fading it with the gradient alpha, in horizontal bands.
void mask_layer_compose(float x, float y, float w, float h, const std::vector<MaskStop>& stops, bool reversed);

} // namespace linweb

namespace linweb {

// Text-shadow layer: the glyphs of a shadowed text box are drawn into their own target with a max-alpha blend, so
// overlapping blurred glyphs do not add up. The layer is then composed once, with normal alpha blending, over whatever
// target is current (including a mask layer). begin returns false if the layer could not be opened.
bool text_shadow_layer_begin(GLint& previous_framebuffer);
void text_shadow_layer_end(GLint previous_framebuffer);
void text_shadow_layer_compose(float x, float y, float w, float h, float opacity);
// The layer's texture and size (used by the blur).
GLuint text_shadow_layer_texture();
int text_shadow_layer_width();
int text_shadow_layer_height();

} // namespace linweb

namespace linweb {

// Gaussian blur of the text-shadow layer with standard deviation sigma (in screen pixels; CSS blur radius / 2),
// done as two separable passes over the region (x, y, w, h): horizontal into an intermediate target, then vertical
// composited over the current target with the given opacity.
void text_shadow_layer_blur_compose(float x, float y, float w, float h, float sigma, float opacity);

} // namespace linweb
