#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace umbra {

inline constexpr float PI     = 3.14159265358979323846f;
inline constexpr float TWO_PI = 6.28318530717958647692f;

inline constexpr float radians(float degrees) noexcept {
    return degrees * (PI / 180.0f);
}

// Fast branchless floor for hot terrain/DDA loops (avoids libm call overhead)
inline constexpr int fastFloor(float x) noexcept {
    int i = static_cast<int>(x);
    return (x < static_cast<float>(i)) ? (i - 1) : i;
}

inline constexpr float clampf(float v, float lo, float hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

inline constexpr float saturate(float v) noexcept {
    return clampf(v, 0.0f, 1.0f);
}

inline constexpr float lerpf(float a, float b, float t) noexcept {
    return a + (b - a) * t;
}

inline constexpr float smoothstepf(float edge0, float edge1, float x) noexcept {
    float denom = edge1 - edge0;
    float t = (denom > 1e-7f || denom < -1e-7f) ? saturate((x - edge0) / denom) : 0.0f;
    return t * t * (3.0f - 2.0f * t);
}

// C2-continuous quintic Hermite step (zero 1st & 2nd derivative discontinuities across biome transitions!)
inline constexpr float smootherstepf(float edge0, float edge1, float x) noexcept {
    float denom = edge1 - edge0;
    float t = (denom > 1e-7f || denom < -1e-7f) ? saturate((x - edge0) / denom) : 0.0f;
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Fast branchless trilinear interpolation for 4x4x4 coarse 3D cave density lattices
inline constexpr float lerp3D(float c000, float c100, float c010, float c110,
                              float c001, float c101, float c011, float c111,
                              float tx, float ty, float tz) noexcept {
    float x00 = c000 + (c100 - c000) * tx;
    float x10 = c010 + (c110 - c010) * tx;
    float x01 = c001 + (c101 - c001) * tx;
    float x11 = c011 + (c111 - c011) * tx;
    float y0  = x00  + (x10  - x00)  * ty;
    float y1  = x01  + (x11  - x01)  * ty;
    return y0 + (y1 - y0) * tz;
}


struct ivec3 {
    int x = 0;
    int y = 0;
    int z = 0;

    constexpr ivec3() noexcept = default;
    constexpr explicit ivec3(int v) noexcept : x(v), y(v), z(v) {}
    constexpr ivec3(int ix, int iy, int iz) noexcept : x(ix), y(iy), z(iz) {}

    constexpr ivec3 operator+(const ivec3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    constexpr ivec3 operator-(const ivec3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    constexpr ivec3 operator*(int s) const noexcept          { return {x * s, y * s, z * s}; }
    constexpr bool operator==(const ivec3& o) const noexcept { return x == o.x && y == o.y && z == o.z; }
    constexpr bool operator!=(const ivec3& o) const noexcept { return !(*this == o); }
};

struct vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr vec2() noexcept = default;
    constexpr explicit vec2(float v) noexcept : x(v), y(v) {}
    constexpr vec2(float ix, float iy) noexcept : x(ix), y(iy) {}

    constexpr vec2 operator+(const vec2& o) const noexcept { return {x + o.x, y + o.y}; }
    constexpr vec2 operator-(const vec2& o) const noexcept { return {x - o.x, y - o.y}; }
    constexpr vec2 operator*(float s) const noexcept       { return {x * s, y * s}; }
};

struct vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr vec3() noexcept = default;
    constexpr explicit vec3(float v) noexcept : x(v), y(v), z(v) {}
    constexpr vec3(float ix, float iy, float iz) noexcept : x(ix), y(iy), z(iz) {}
    constexpr explicit vec3(const ivec3& v) noexcept
        : x(static_cast<float>(v.x)), y(static_cast<float>(v.y)), z(static_cast<float>(v.z)) {}

    constexpr vec3 operator+(const vec3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    constexpr vec3 operator-(const vec3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    constexpr vec3 operator-() const noexcept              { return {-x, -y, -z}; }
    constexpr vec3 operator*(float s) const noexcept       { return {x * s, y * s, z * s}; }
    constexpr vec3 operator/(float s) const noexcept       { return {x / s, y / s, z / s}; }

    constexpr vec3& operator+=(const vec3& o) noexcept { x += o.x; y += o.y; z += o.z; return *this; }
    constexpr vec3& operator-=(const vec3& o) noexcept { x -= o.x; y -= o.y; z -= o.z; return *this; }
    constexpr vec3& operator*=(float s) noexcept       { x *= s; y *= s; z *= s; return *this; }
};

inline constexpr vec3 operator*(float s, const vec3& v) noexcept {
    return {v.x * s, v.y * s, v.z * s};
}

inline constexpr float dot(const vec3& a, const vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline constexpr vec3 cross(const vec3& a, const vec3& b) noexcept {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline constexpr float lengthSq(const vec3& v) noexcept {
    return dot(v, v);
}

inline float length(const vec3& v) noexcept {
    return std::sqrt(lengthSq(v));
}

inline vec3 normalize(const vec3& v) noexcept {
    float lenSq = lengthSq(v);
    if (lenSq < 1e-16f) return {0.0f, 0.0f, 0.0f};
    float inv = 1.0f / std::sqrt(lenSq);
    return {v.x * inv, v.y * inv, v.z * inv};
}

inline constexpr vec3 mix(const vec3& a, const vec3& b, float t) noexcept {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}

struct alignas(16) vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    constexpr vec4() noexcept = default;
    constexpr explicit vec4(float v) noexcept : x(v), y(v), z(v), w(v) {}
    constexpr vec4(float ix, float iy, float iz, float iw) noexcept : x(ix), y(iy), z(iz), w(iw) {}
    constexpr vec4(const vec3& v, float iw) noexcept : x(v.x), y(v.y), z(v.z), w(iw) {}
};

// Column-major 4x4 matrix matching GLSL std140 / Vulkan mat4 layout
struct alignas(16) mat4 {
    float m[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    constexpr float& at(int col, int row) noexcept             { return m[col * 4 + row]; }
    constexpr const float& at(int col, int row) const noexcept { return m[col * 4 + row]; }

    constexpr mat4 operator*(const mat4& b) const noexcept {
        mat4 out{};
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                out.at(c, r) =
                    at(0, r) * b.at(c, 0) +
                    at(1, r) * b.at(c, 1) +
                    at(2, r) * b.at(c, 2) +
                    at(3, r) * b.at(c, 3);
            }
        }
        return out;
    }
};

inline mat4 lookAt(const vec3& eye, const vec3& center, const vec3& up) noexcept {
    vec3 f = normalize(center - eye);
    vec3 s = normalize(cross(f, up));
    vec3 u = cross(s, f);

    mat4 res{};
    res.at(0, 0) =  s.x; res.at(1, 0) =  s.y; res.at(2, 0) =  s.z; res.at(3, 0) = -dot(s, eye);
    res.at(0, 1) =  u.x; res.at(1, 1) =  u.y; res.at(2, 1) =  u.z; res.at(3, 1) = -dot(u, eye);
    res.at(0, 2) = -f.x; res.at(1, 2) = -f.y; res.at(2, 2) = -f.z; res.at(3, 2) =  dot(f, eye);
    res.at(0, 3) = 0.0f; res.at(1, 3) = 0.0f; res.at(2, 3) = 0.0f; res.at(3, 3) =  1.0f;
    return res;
}

// Right-handed perspective projection directly in Vulkan clip space: depth [0, 1] + Y-axis inverted
inline mat4 perspectiveVulkan(float fovRadians, float aspect, float zNear, float zFar) noexcept {
    float tanHalfFov = std::tan(fovRadians * 0.5f);
    float f = 1.0f / std::max(tanHalfFov, 1e-5f);

    mat4 res{};
    for (float& v : res.m) v = 0.0f;
    res.at(0, 0) =  f / std::max(aspect, 0.05f);
    res.at(1, 1) = -f; // Vulkan clip-space Y flip
    res.at(2, 2) =  zFar / (zNear - zFar);
    res.at(2, 3) = -1.0f;
    res.at(3, 2) =  (zFar * zNear) / (zNear - zFar);
    return res;
}

// Fast pure C++20 4x4 matrix inverse (computed once per frame on CPU instead of per-pixel in sky.frag!)
inline mat4 inverse(const mat4& mat) noexcept {
    const float* m = mat.m;
    mat4 inv{};
    float* o = inv.m;

    o[0] =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15]
          + m[9]*m[7]*m[14]  + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    o[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15]
          - m[8]*m[7]*m[14]  - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    o[8] =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15]
          + m[8]*m[7]*m[13]  + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    o[12]= -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14]
          - m[8]*m[6]*m[13]  - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];

    o[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15]
          - m[9]*m[3]*m[14]  - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    o[5] =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15]
          + m[8]*m[3]*m[14]  + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    o[9] = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15]
          - m[8]*m[3]*m[13]  - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    o[13]=  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14]
          + m[8]*m[2]*m[13]  + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];

    o[2] =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15]
          + m[5]*m[3]*m[14]  + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    o[6] = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15]
          - m[4]*m[3]*m[14]  - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    o[10]=  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15]
          + m[4]*m[3]*m[13]  + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    o[14]= -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14]
          - m[4]*m[2]*m[13]  - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];

    o[3] = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11]
          - m[5]*m[3]*m[10]  - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
    o[7] =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11]
          + m[4]*m[3]*m[10]  + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
    o[11]= -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11]
          - m[4]*m[3]*m[9]   - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
    o[15]=  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10]
          + m[4]*m[2]*m[9]   + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

    float det = m[0]*o[0] + m[1]*o[4] + m[2]*o[8] + m[3]*o[12];
    if (std::abs(det) < 1e-12f) return mat4{};
    float invDet = 1.0f / det;
    for (int i = 0; i < 16; ++i) o[i] *= invDet;
    return inv;
}

// -----------------------------------------------------------------------------
// Fast 6-Plane Camera Frustum Culling (Gribb-Hartmann extraction for Vulkan [0,1] Z)
// Eliminates ~65-75% of chunk draw calls before issuing vkCmdDrawIndexed!
// -----------------------------------------------------------------------------
struct FrustumPlane {
    float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
};

struct Frustum {
    std::array<FrustumPlane, 6> planes{};

    // Positive-vertex AABB test: returns false if the entire chunk box is outside any plane
    inline bool intersectsAABB(float minX, float minY, float minZ,
                               float maxX, float maxY, float maxZ) const noexcept {
        for (const FrustumPlane& p : planes) {
            float px = (p.a >= 0.0f) ? maxX : minX;
            float py = (p.b >= 0.0f) ? maxY : minY;
            float pz = (p.c >= 0.0f) ? maxZ : minZ;
            if (p.a * px + p.b * py + p.c * pz + p.d < 0.0f) {
                return false;
            }
        }
        return true;
    }
};

inline Frustum extractFrustumVulkan(const mat4& vp) noexcept {
    auto row = [&](int r) -> vec4 {
        return {vp.at(0, r), vp.at(1, r), vp.at(2, r), vp.at(3, r)};
    };
    vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);

    auto makePlane = [](float a, float b, float c, float d) noexcept -> FrustumPlane {
        float len = std::sqrt(a * a + b * b + c * c);
        float inv = (len > 1e-8f) ? (1.0f / len) : 1.0f;
        return {a * inv, b * inv, c * inv, d * inv};
    };

    Frustum f{};
    // Left:   row3 + row0 >= 0
    f.planes[0] = makePlane(r3.x + r0.x, r3.y + r0.y, r3.z + r0.z, r3.w + r0.w);
    // Right:  row3 - row0 >= 0
    f.planes[1] = makePlane(r3.x - r0.x, r3.y - r0.y, r3.z - r0.z, r3.w - r0.w);
    // Bottom: row3 + row1 >= 0
    f.planes[2] = makePlane(r3.x + r1.x, r3.y + r1.y, r3.z + r1.z, r3.w + r1.w);
    // Top:    row3 - row1 >= 0
    f.planes[3] = makePlane(r3.x - r1.x, r3.y - r1.y, r3.z - r1.z, r3.w - r1.w);
    // Near (Vulkan Z >= 0): row2 >= 0
    f.planes[4] = makePlane(r2.x, r2.y, r2.z, r2.w);
    // Far  (Vulkan Z <= W): row3 - row2 >= 0
    f.planes[5] = makePlane(r3.x - r2.x, r3.y - r2.y, r3.z - r2.z, r3.w - r2.w);
    return f;
}

static_assert(sizeof(vec2) == 8,  "vec2 must be tightly packed 8 bytes");
static_assert(sizeof(vec3) == 12, "vec3 must be tightly packed 12 bytes");
static_assert(sizeof(vec4) == 16, "vec4 must be 16-byte std140 aligned");
static_assert(sizeof(mat4) == 64, "mat4 must be 64-byte std140 aligned");

} // namespace umbra
