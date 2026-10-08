#ifndef OGL_MATH_H
#define OGL_MATH_H

#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class Vec3 {
public:
    float x, y, z;

    Vec3(float x = 0.0f, float y = 0.0f, float z = 0.0f) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& other) const {
        return Vec3(x + other.x, y + other.y, z + other.z);
    }

    Vec3 operator-(const Vec3& other) const {
        return Vec3(x - other.x, y - other.y, z - other.z);
    }

    Vec3 operator*(float s) const {
        return Vec3(x * s, y * s, z * s);
    }

    float dot(const Vec3& other) const {
        return x * other.x + y * other.y + z * other.z;
    }

    Vec3 cross(const Vec3& other) const {
        return Vec3(
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x
        );
    }

    float length() const {
        return std::sqrt(dot(*this));
    }

    Vec3 normalize() const {
        float len = length();
        if (len > 0.0f) return (*this) * (1.0f / len);
        return *this;
    }
};

class Mat4 {
public:
    float m[16];

    Mat4() {
        std::memset(m, 0, sizeof(m));
    }

    static Mat4 identity() {
        Mat4 res;
        res.m[0] = 1.0f; res.m[5] = 1.0f; res.m[10] = 1.0f; res.m[15] = 1.0f;
        return res;
    }

    Mat4 operator*(const Mat4& other) const {
        Mat4 res;
        for (int c = 0; c < 4; c++) {
            for (int r = 0; r < 4; r++) {
                for (int k = 0; k < 4; k++) {
                    res.m[c * 4 + r] += m[k * 4 + r] * other.m[c * 4 + k];
                }
            }
        }
        return res;
    }

    static Mat4 perspective(float fov, float aspect, float near, float far) {
        Mat4 res;
        float tanHalfFov = std::tan(fov / 2.0f);
        res.m[0] = 1.0f / (aspect * tanHalfFov);
        res.m[5] = 1.0f / tanHalfFov;
        res.m[10] = -(far + near) / (far - near);
        res.m[11] = -1.0f;
        res.m[14] = -(2.0f * far * near) / (far - near);
        return res;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = (center - eye).normalize();
        Vec3 s = f.cross(up).normalize();
        Vec3 u = s.cross(f);

        Mat4 res = Mat4::identity();
        res.m[0] = s.x;
        res.m[1] = u.x;
        res.m[2] = -f.x;
        res.m[4] = s.y;
        res.m[5] = u.y;
        res.m[6] = -f.y;
        res.m[8] = s.z;
        res.m[9] = u.z;
        res.m[10] = -f.z;
        res.m[12] = -s.dot(eye);
        res.m[13] = -u.dot(eye);
        res.m[14] = f.dot(eye);
        return res;
    }

    static Mat4 rotate(const Mat4& m, float angle, const Vec3& axis) {
        float c = std::cos(angle);
        float s = std::sin(angle);
        Vec3 a = axis.normalize();
        Vec3 temp = a * (1.0f - c);

        Mat4 rot = Mat4::identity();
        rot.m[0] = c + temp.x * a.x;
        rot.m[1] = temp.x * a.y + s * a.z;
        rot.m[2] = temp.x * a.z - s * a.y;

        rot.m[4] = temp.y * a.x - s * a.z;
        rot.m[5] = c + temp.y * a.y;
        rot.m[6] = temp.y * a.z + s * a.x;

        rot.m[8] = temp.z * a.x + s * a.y;
        rot.m[9] = temp.z * a.y - s * a.x;
        rot.m[10] = c + temp.z * a.z;

        return m * rot;
    }
};

#endif
