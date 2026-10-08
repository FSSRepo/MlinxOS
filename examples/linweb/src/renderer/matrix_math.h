#pragma once
#include <array>
#include <cmath>

namespace linweb {

inline std::array<float, 16> mat4_identity()
{
    return {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1};
}

inline std::array<float, 16> mat4_translate(float x, float y)
{
    auto m = mat4_identity();
    m[12] = x;
    m[13] = y;
    return m;
}

inline std::array<float, 16> mat4_rotate(float degrees)
{
    float rad = -degrees * 3.14159f / 180.0f;
    float c = cosf(rad);
    float s = sinf(rad);
    auto m = mat4_identity();
    m[0] = c;
    m[1] = s;
    m[4] = -s;
    m[5] = c;
    return m;
}

inline std::array<float, 16> mat4_scale(float x, float y)
{
    auto m = mat4_identity();
    m[0] = x;
    m[5] = y;
    return m;
}

inline std::array<float, 16> mat4_mul(const std::array<float, 16> &a, const std::array<float, 16> &b)
{
    std::array<float, 16> res{};
    for (int i = 0; i < 4; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            for (int k = 0; k < 4; ++k)
            {
                res[i * 4 + j] += a[k * 4 + j] * b[i * 4 + k];
            }
        }
    }
    return res;
}

} // namespace linweb
