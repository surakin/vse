#pragma once

#include "log/log.h"
#include "math/math_util.h"
#include "types.h"

// Vec2/Vec3/Vec4: plain component structs, no SIMD/alignment tricks yet
// (PLAN.md assumption #2: hand-rolled math). Index access goes through an
// explicit switch rather than a union/pointer-arithmetic trick, to avoid
// relying on layout behavior the standard doesn't strictly guarantee.
namespace engine_placeholder {

struct Vec2 {
    f32 x = 0.0f;
    f32 y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(f32 x_, f32 y_) : x(x_), y(y_) {}

    f32& operator[](usize i) { return i == 0 ? x : y; }
    f32 operator[](usize i) const { return i == 0 ? x : y; }

    Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
    Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
    Vec2 operator*(f32 s) const { return Vec2(x * s, y * s); }

    f32 Dot(const Vec2& o) const { return x * o.x + y * o.y; }

    f32 LengthSquared() const { return Dot(*this); }
    f32 Length() const { return Sqrt(LengthSquared()); }

    Vec2 Normalized() const {
        f32 len = Length();
        ENGINE_ASSERT(len > 0.0f, "Normalized() of a zero-length Vec2");
        return Vec2(x / len, y / len);
    }

    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vec2& o) const { return !(*this == o); }
};

struct Vec3 {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(f32 x_, f32 y_, f32 z_) : x(x_), y(y_), z(z_) {}

    f32& operator[](usize i) {
        switch (i) {
            case 0:
                return x;
            case 1:
                return y;
            default:
                return z;
        }
    }
    f32 operator[](usize i) const {
        switch (i) {
            case 0:
                return x;
            case 1:
                return y;
            default:
                return z;
        }
    }

    Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator*(f32 s) const { return Vec3(x * s, y * s, z * s); }

    f32 Dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }

    Vec3 Cross(const Vec3& o) const { return Vec3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x); }

    f32 LengthSquared() const { return Dot(*this); }
    f32 Length() const { return Sqrt(LengthSquared()); }

    Vec3 Normalized() const {
        f32 len = Length();
        ENGINE_ASSERT(len > 0.0f, "Normalized() of a zero-length Vec3");
        return Vec3(x / len, y / len, z / len);
    }

    bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const Vec3& o) const { return !(*this == o); }
};

struct Vec4 {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 0.0f;

    constexpr Vec4() = default;
    constexpr Vec4(f32 x_, f32 y_, f32 z_, f32 w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(const Vec3& v, f32 w_) : x(v.x), y(v.y), z(v.z), w(w_) {}

    f32& operator[](usize i) {
        switch (i) {
            case 0:
                return x;
            case 1:
                return y;
            case 2:
                return z;
            default:
                return w;
        }
    }
    f32 operator[](usize i) const {
        switch (i) {
            case 0:
                return x;
            case 1:
                return y;
            case 2:
                return z;
            default:
                return w;
        }
    }

    Vec4 operator+(const Vec4& o) const { return Vec4(x + o.x, y + o.y, z + o.z, w + o.w); }
    Vec4 operator-(const Vec4& o) const { return Vec4(x - o.x, y - o.y, z - o.z, w - o.w); }
    Vec4 operator*(f32 s) const { return Vec4(x * s, y * s, z * s, w * s); }

    f32 Dot(const Vec4& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }

    f32 LengthSquared() const { return Dot(*this); }
    f32 Length() const { return Sqrt(LengthSquared()); }

    Vec4 Normalized() const {
        f32 len = Length();
        ENGINE_ASSERT(len > 0.0f, "Normalized() of a zero-length Vec4");
        return Vec4(x / len, y / len, z / len, w / len);
    }

    bool operator==(const Vec4& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
    bool operator!=(const Vec4& o) const { return !(*this == o); }
};

} // namespace engine_placeholder
