#include "ps3_backgrounds.h"
#include "gl_wrapper.h"
#include "renderer/renderer.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>

namespace {

// "Ring" background (port of the Shadertoy ring.glsl shader), in navy blue: a glowing ring made of simplex noise. It
// follows the music: the ring opens and lights up with the loudness (uLevel) and the bass (uBass). The shader draws the
// box it is given: uSize is its size in pixels, uTime the time of the ring in seconds (it runs faster with the music).
const std::string kRingBody =
    "uniform float uTime;\n"
    "uniform vec2 uSize;\n"
    "uniform float uLevel;\n"
    "uniform float uBass;\n"
    "uniform float uOpacity;\n"
    "varying vec2 vUV;\n"
    "const vec3 MOD3 = vec3(0.1031, 0.11369, 0.13787);\n"
    "const vec3 DEEP = vec3(3.0, 6.0, 24.0) / 255.0;\n"
    "vec3 hash33(vec3 p3) {\n"
    "  p3 = fract(p3 * MOD3);\n"
    "  p3 += dot(p3, p3.yxz + 19.19);\n"
    "  return -1.0 + 2.0 * fract(vec3((p3.x + p3.y) * p3.z, (p3.x + p3.z) * p3.y, (p3.y + p3.z) * p3.x));\n"
    "}\n"
    "float simplex_noise(vec3 p) {\n"
    "  const float K1 = 0.333333333;\n"
    "  const float K2 = 0.166666667;\n"
    "  vec3 i = floor(p + (p.x + p.y + p.z) * K1);\n"
    "  vec3 d0 = p - (i - (i.x + i.y + i.z) * K2);\n"
    "  vec3 e = step(vec3(0.0), d0 - d0.yzx);\n"
    "  vec3 i1 = e * (1.0 - e.zxy);\n"
    "  vec3 i2 = 1.0 - e.zxy * (1.0 - e);\n"
    "  vec3 d1 = d0 - (i1 - 1.0 * K2);\n"
    "  vec3 d2 = d0 - (i2 - 2.0 * K2);\n"
    "  vec3 d3 = d0 - (1.0 - 3.0 * K2);\n"
    "  vec4 h = max(0.6 - vec4(dot(d0, d0), dot(d1, d1), dot(d2, d2), dot(d3, d3)), 0.0);\n"
    "  vec4 n = h * h * h * h * vec4(dot(d0, hash33(i)), dot(d1, hash33(i + i1)), dot(d2, hash33(i + i2)), dot(d3, hash33(i + 1.0)));\n"
    "  return dot(vec4(31.316), n);\n"
    "}\n"
    "void main() {\n"
    "  vec2 fragCoord = vec2(vUV.x, 1.0 - vUV.y) * uSize;\n"
    "  vec2 uv = (fragCoord - uSize * 0.5) / uSize.y;\n"
    "  uv *= 1.0 - 0.10 * uBass;\n"
    "  float a = sin(atan(uv.y, uv.x));\n"
    "  float am = abs(a - 0.5) / 4.0;\n"
    "  float l = length(uv);\n"
    "  float m1 = clamp(0.1 / smoothstep(0.0, 1.75, l), 0.0, 1.0);\n"
    "  float m2 = clamp(0.1 / smoothstep(0.42, 0.0, l), 0.0, 1.0);\n"
    "  float s1 = (simplex_noise(vec3(uv * 2.0, 1.0 + uTime * 0.525)) * (max(1.0 - l * 1.75, 0.0)) + 0.9);\n"
    "  float s2 = (simplex_noise(vec3(uv * 1.0, 15.0 + uTime * 0.525)) * (max(0.0 + l * 1.0, 0.025)) + 1.25);\n"
    "  float s3 = (simplex_noise(vec3(vec2(am, am * 100.0 + uTime * 3.0) * 0.15, 30.0 + uTime * 0.525)) * (max(0.0 + l * 1.0, 0.25)) + 1.5);\n"
    "  s3 *= smoothstep(0.0, 0.3345, l);\n"
    "  float sh = smoothstep(0.15, 0.35, l);\n"
    "  float m = m1 * m1 * m2 * ((s1 * s2 * s3) * (1.0 - l)) * sh;\n"
    "  m *= 1.0 + 1.2 * uLevel + 0.8 * uBass;\n"
    "  float tone = 0.5 + 0.5 * sin(uTime * 0.6 + uv.x * 3.0 + uv.y * 2.0);\n"
    "  vec3 blue = mix(vec3(0.04, 0.22, 0.95), vec3(0.3, 0.8, 1.0), tone);\n"
    "  vec3 col = mix(DEEP, blue, m);\n"
    "  col += vec3(0.1, 0.3, 0.9) * uBass * 0.25 * exp(-3.0 * l);\n"
    "  gl_FragColor = vec4(col, uOpacity);\n"
    "}\n";

const std::string kRingFragmentGles =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "precision highp float;\n"
    "#else\n"
    "precision mediump float;\n"
    "#endif\n" +
    kRingBody;

const std::string kRingFragmentGl =
    "#version 110\n" +
    kRingBody;

// The last values given to set_music_levels(), and the smoothed ones the shader gets: they rise fast and fall slowly.
float g_level = 0.0f;
float g_bass = 0.0f;
float ring_level = 0.0f;
float ring_bass = 0.0f;
float ring_time = 0.0f;
float ring_last_time = -1.0f;   // the engine time of the last ring draw; NaN would be worse, so -1 means "not drawn yet"

// LINWEB_FAKE_AUDIO makes a beat up, to see the ring where no MP3 can play.
bool fake_audio() {
    static const bool enabled = std::getenv("LINWEB_FAKE_AUDIO") != nullptr;
    return enabled;
}

void set_ring_uniforms(unsigned program, float, float, float width, float height, float opacity, double time) {
    // Cache the locations: the callback runs for every box that has the background, on every frame.
    static unsigned cached_program = 0;
    static int u_time = -1, u_size = -1, u_level = -1, u_bass = -1, u_opacity = -1;
    if (program != cached_program) {
        cached_program = program;
        u_time = glGetUniformLocation(program, "uTime");
        u_size = glGetUniformLocation(program, "uSize");
        u_level = glGetUniformLocation(program, "uLevel");
        u_bass = glGetUniformLocation(program, "uBass");
        u_opacity = glGetUniformLocation(program, "uOpacity");
    }

    const float t = static_cast<float>(time);
    const float dt = ring_last_time < 0.0f ? 0.0f : std::max(0.0f, std::min(0.1f, t - ring_last_time));
    ring_last_time = t;

    float level = g_level, bass = g_bass;
    if (fake_audio()) {
        const float beat = static_cast<float>(std::pow(std::max(0.0f, std::sin(t * 5.5f)), 6.0f));
        level = 0.25f + 0.55f * beat;
        bass = beat;
    }

    // the ring follows the music: the loudness is smoothed (up fast, down slowly) and the clock of the shader runs
    // faster the louder it is, so the ring moves with the beat without jumping
    auto follow = [dt](float& smoothed, float target) {
        const float rate = target > smoothed ? 20.0f : 4.0f;
        smoothed += (target - smoothed) * std::min(1.0f, rate * dt);
    };
    follow(ring_level, level);
    follow(ring_bass, bass);
    ring_time += dt * (1.0f + 2.5f * ring_level);

    // the opacity of the element (the fade of its panel) is the alpha of the picture
    if (u_time >= 0) glUniform1f(u_time, ring_time);
    if (u_size >= 0) glUniform2f(u_size, width, height);
    if (u_level >= 0) glUniform1f(u_level, ring_level);
    if (u_bass >= 0) glUniform1f(u_bass, ring_bass);
    if (u_opacity >= 0) glUniform1f(u_opacity, std::max(0.0f, std::min(1.0f, opacity)));
}

} // namespace

void register_ps3_backgrounds() {
    linweb::ShaderBackgroundSpec ring;
    ring.name = "ring";
    ring.fragment_gles = kRingFragmentGles;
    ring.fragment_gl = kRingFragmentGl;
    ring.animated = true;
    ring.set_uniforms = set_ring_uniforms;
    linweb::Renderer::register_shader_background(std::move(ring));
}

void set_music_levels(float level, float bass) {
    if (fake_audio()) return;   // the ring makes its own beat
    g_level = std::max(0.0f, std::min(1.0f, level));
    g_bass = std::max(0.0f, std::min(1.0f, bass));
}
