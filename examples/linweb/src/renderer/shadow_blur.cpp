#include "renderer/mask_renderer.h"
#include "renderer/renderer_internal.h"
#include "renderer/shader_manager.h"
#include <algorithm>
#include <cmath>

namespace linweb {

namespace {

// Vertex and fragment sources of one separable gaussian pass: taps along uStep, weights exp(-i^2 / (2 sigma^2)),
// up to uRadius taps each side. The fragment colour is premultiplied, so the sum of the taps is too.
const char* kBlurVertexSource =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "uniform mat4 uProj;\n"
    "varying vec2 vUV;\n"
    "void main(){ vUV = aUV; gl_Position = uProj * vec4(aPos, 0.0, 1.0); }\n";

const char* kBlurFragmentSource =
    "precision mediump float;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uStep;\n"
    "uniform float uRadius;\n"
    "uniform float uSigma;\n"
    "uniform float uOpacity;\n"
    "varying vec2 vUV;\n"
    "void main(){\n"
    "  float inv = 1.0 / (2.0 * uSigma * uSigma);\n"
    "  vec4 sum = vec4(0.0);\n"
    "  float wsum = 0.0;\n"
    "  for (int i = -64; i <= 64; ++i) {\n"
    "    float fi = float(i);\n"
    "    if (abs(fi) > uRadius) continue;\n"
    "    float w = exp(-fi * fi * inv);\n"
    "    sum += texture2D(uTex, vUV + uStep * fi) * w;\n"
    "    wsum += w;\n"
    "  }\n"
    "  gl_FragColor = (sum / wsum) * uOpacity;\n"
    "}\n";

GLuint g_blur_program = 0;
GLint g_blur_u_proj = -1, g_blur_u_tex = -1, g_blur_u_step = -1, g_blur_u_radius = -1, g_blur_u_sigma = -1, g_blur_u_opacity = -1;
GLint g_blur_a_pos = -1, g_blur_a_uv = -1;

GLuint g_blur_fbo = 0;
GLuint g_blur_tex = 0;
int g_blur_w = 0;
int g_blur_h = 0;

bool ensure_blur_program()
{
    if (g_blur_program) return true;
    GLuint vs = compile_shader(GL_VERTEX_SHADER, kBlurVertexSource);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kBlurFragmentSource);
    if (!vs || !fs) return false;
    g_blur_program = link_program(vs, fs);
    if (!g_blur_program) return false;
    g_blur_u_proj = glGetUniformLocation(g_blur_program, "uProj");
    g_blur_u_tex = glGetUniformLocation(g_blur_program, "uTex");
    g_blur_u_step = glGetUniformLocation(g_blur_program, "uStep");
    g_blur_u_radius = glGetUniformLocation(g_blur_program, "uRadius");
    g_blur_u_sigma = glGetUniformLocation(g_blur_program, "uSigma");
    g_blur_u_opacity = glGetUniformLocation(g_blur_program, "uOpacity");
    g_blur_a_pos = glGetAttribLocation(g_blur_program, "aPos");
    g_blur_a_uv = glGetAttribLocation(g_blur_program, "aUV");
    return true;
}

// Draws the region (x, y, w, h) with the blur program. Texture coordinates follow the layer convention (layout row y
// is texture row height - y), so each pass samples the place it was written to.
void draw_blur_region(float x, float y, float w, float h, float step_x, float step_y, float radius, float sigma, float opacity)
{
    const float vw = static_cast<float>(g_blur_w);
    const float vh = static_cast<float>(g_blur_h);
    const float u0 = x / vw, u1 = (x + w) / vw;
    const float v_top = (vh - y) / vh, v_bottom = (vh - (y + h)) / vh;
    const float verts[] = {
        x,     y,     u0, v_top,
        x + w, y,     u1, v_top,
        x + w, y + h, u1, v_bottom,

        x,     y,     u0, v_top,
        x + w, y + h, u1, v_bottom,
        x,     y + h, u0, v_bottom
    };
    glUseProgram(g_blur_program);
    glUniformMatrix4fv(g_blur_u_proj, 1, GL_FALSE, RendererState::proj.data());
    glUniform1i(g_blur_u_tex, 0);
    glUniform2f(g_blur_u_step, step_x, step_y);
    glUniform1f(g_blur_u_radius, radius);
    glUniform1f(g_blur_u_sigma, sigma);
    glUniform1f(g_blur_u_opacity, opacity);
    glBindBuffer(GL_ARRAY_BUFFER, RendererState::vbo_color);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
    glEnableVertexAttribArray(g_blur_a_pos);
    glVertexAttribPointer(g_blur_a_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(g_blur_a_uv);
    glVertexAttribPointer(g_blur_a_uv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(g_blur_a_pos);
    glDisableVertexAttribArray(g_blur_a_uv);
}

} // namespace

void text_shadow_layer_blur_compose(float x, float y, float w, float h, float sigma, float opacity)
{
    const GLuint source = text_shadow_layer_texture();
    if (source == 0 || w <= 0.0f || h <= 0.0f || sigma <= 0.0f) return;
    if (!ensure_blur_program()) return;

    const int vw = RendererState::viewport_width;
    const int vh = RendererState::viewport_height;
    // the source layer and the blur target have the viewport size
    if (text_shadow_layer_width() != vw || text_shadow_layer_height() != vh) return;

    if (!g_blur_fbo) glGenFramebuffers(1, &g_blur_fbo);
    if (!g_blur_tex) glGenTextures(1, &g_blur_tex);
    if (g_blur_w != vw || g_blur_h != vh) {
        glBindTexture(GL_TEXTURE_2D, g_blur_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, vw, vh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g_blur_w = vw;
        g_blur_h = vh;
    }

    // the gaussian reaches 3 sigma: the region is widened by that, so the glow is not cut at the edges
    const float reach = std::ceil(3.0f * sigma);
    const float radius = std::min(reach, 64.0f);
    const float rx = x - reach, ry = y - reach, rw = w + 2.0f * reach, rh = h + 2.0f * reach;

    GLint previous_framebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);
    GLint scissor_box[4];
    glGetIntegerv(GL_SCISSOR_BOX, scissor_box);
    const GLboolean scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean blend_enabled = glIsEnabled(GL_BLEND);
    GLint saved_src = GL_SRC_ALPHA, saved_dst = GL_ONE_MINUS_SRC_ALPHA;
    glGetIntegerv(GL_BLEND_SRC_RGB, &saved_src);
    glGetIntegerv(GL_BLEND_DST_RGB, &saved_dst);

    // the region in scissor coordinates (origin at the bottom of the window)
    glEnable(GL_SCISSOR_TEST);
    glScissor(static_cast<int>(std::floor(rx)), static_cast<int>(std::floor(vh - (ry + rh))),
              static_cast<int>(std::ceil(rw)) + 1, static_cast<int>(std::ceil(rh)) + 1);
    glDisable(GL_BLEND);

    // horizontal pass: the layer into the intermediate target
    glBindFramebuffer(GL_FRAMEBUFFER, g_blur_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_blur_tex, 0);
    glViewport(0, 0, vw, vh);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source);
    draw_blur_region(rx, ry, rw, rh, 1.0f / static_cast<float>(vw), 0.0f, radius, sigma, 1.0f);

    // vertical pass: the blurred intermediate, composited over the previous target with the opacity
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
    glViewport(0, 0, vw, vh);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, g_blur_tex);
    draw_blur_region(rx, ry, rw, rh, 0.0f, 1.0f / static_cast<float>(vh), radius, sigma, opacity);

    // restore the state the caller had
    glUseProgram(0);
    glBlendFuncSeparate(static_cast<GLenum>(saved_src), static_cast<GLenum>(saved_dst), GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (blend_enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (scissor_enabled) glScissor(scissor_box[0], scissor_box[1], scissor_box[2], scissor_box[3]);
    else glDisable(GL_SCISSOR_TEST);
}

} // namespace linweb
