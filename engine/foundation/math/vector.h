#pragma once

#include "log/log.h"
#include "math/math_util.h"
#include "types.h"

//! Vec2, Vec3 and Vec4: plain float component structs with basic arithmetic.
//!
//! No SIMD or special alignment. Index access uses an explicit switch rather
//! than relying on struct layout.
namespace vse
{

/// 2-component float vector with members `x, y`.
///
/// Components default to 0. Equality compares components exactly.
///
/// - Thread-safety: thread-compatible plain data.
struct Vec2
{
    /// X component.
    f32 x = 0.0f;
    /// Y component.
    f32 y = 0.0f;

    /// Creates the zero vector.
    constexpr Vec2() = default;
    /// Creates a vector from its components.
    constexpr Vec2(f32 x_, f32 y_) : x(x_), y(y_) {}

    /// Returns a reference to component `i` (0 = x, 1 = y).
    ///
    /// - Param i: component index; any value above 1 returns `y` (not checked).
    f32& operator[](usize i) { return i == 0 ? x : y; }
    /// Returns the value of component `i` (0 = x, 1 = y).
    ///
    /// - Param i: component index; any value above 1 returns `y` (not checked).
    f32 operator[](usize i) const { return i == 0 ? x : y; }

    /// Returns the component-wise sum.
    Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
    /// Returns the component-wise difference.
    Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
    /// Returns the vector with every component multiplied by `s`.
    Vec2 operator*(f32 s) const { return Vec2(x * s, y * s); }

    /// Returns the dot product with `o`.
    f32 Dot(const Vec2& o) const { return x * o.x + y * o.y; }

    /// Returns the squared length; cheaper than `Length`.
    f32 LengthSquared() const { return Dot(*this); }
    /// Returns the Euclidean length.
    f32 Length() const { return Sqrt(LengthSquared()); }

    /// Returns the vector scaled to unit length.
    ///
    /// - Aborts: the length is zero.
    Vec2 Normalized() const
    {
        f32 len = Length();
        ENGINE_ASSERT(len > 0.0f, "Normalized() of a zero-length Vec2");
        return Vec2(x / len, y / len);
    }

    /// Returns true if every component compares exactly equal.
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    /// Returns true if any component differs.
    bool operator!=(const Vec2& o) const { return !(*this == o); }
};

/// 3-component float vector with members `x, y, z`.
///
/// Components default to 0. Equality compares components exactly.
///
/// - Thread-safety: thread-compatible plain data.
struct Vec3
{
    /// X component.
    f32 x = 0.0f;
    /// Y component.
    f32 y = 0.0f;
    /// Z component.
    f32 z = 0.0f;

    /// Creates the zero vector.
    constexpr Vec3() = default;
    /// Creates a vector from its components.
    constexpr Vec3(f32 x_, f32 y_, f32 z_) : x(x_), y(y_), z(z_) {}

    /// Returns a reference to component `i` (0 = x, 1 = y, 2 = z).
    ///
    /// - Param i: component index; any value above 2 returns `z` (not checked).
    f32& operator[](usize i)
    {
        switch (i)
        {
        case 0: return x;
        case 1: return y;
        default: return z;
        }
    }
    /// Returns the value of component `i` (0 = x, 1 = y, 2 = z).
    ///
    /// - Param i: component index; any value above 2 returns `z` (not checked).
    f32 operator[](usize i) const
    {
        switch (i)
        {
        case 0: return x;
        case 1: return y;
        default: return z;
        }
    }

    /// Returns the component-wise sum.
    Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    /// Returns the component-wise difference.
    Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    /// Returns the vector with every component multiplied by `s`.
    Vec3 operator*(f32 s) const { return Vec3(x * s, y * s, z * s); }

    /// Returns the dot product with `o`.
    f32 Dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }

    /// Returns the cross product `this x o` (right-handed).
    Vec3 Cross(const Vec3& o) const { return Vec3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x); }

    /// Returns the squared length; cheaper than `Length`.
    f32 LengthSquared() const { return Dot(*this); }
    /// Returns the Euclidean length.
    f32 Length() const { return Sqrt(LengthSquared()); }

    /// Returns the vector scaled to unit length.
    ///
    /// - Aborts: the length is zero.
    Vec3 Normalized() const
    {
        f32 len = Length();
        ENGINE_ASSERT(len > 0.0f, "Normalized() of a zero-length Vec3");
        return Vec3(x / len, y / len, z / len);
    }

    /// Returns true if every component compares exactly equal.
    bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    /// Returns true if any component differs.
    bool operator!=(const Vec3& o) const { return !(*this == o); }
};

/// 4-component float vector with members `x, y, z, w`.
///
/// Components default to 0. Equality compares components exactly.
///
/// - Thread-safety: thread-compatible plain data.
struct Vec4
{
    /// X component.
    f32 x = 0.0f;
    /// Y component.
    f32 y = 0.0f;
    /// Z component.
    f32 z = 0.0f;
    /// W component.
    f32 w = 0.0f;

    /// Creates the zero vector.
    constexpr Vec4() = default;
    /// Creates a vector from its components.
    constexpr Vec4(f32 x_, f32 y_, f32 z_, f32 w_) : x(x_), y(y_), z(z_), w(w_) {}
    /// Creates a vector from a Vec3 and a `w` component.
    constexpr Vec4(const Vec3& v, f32 w_) : x(v.x), y(v.y), z(v.z), w(w_) {}

    /// Returns a reference to component `i` (0 = x, 1 = y, 2 = z, 3 = w).
    ///
    /// - Param i: component index; any value above 3 returns `w` (not checked).
    f32& operator[](usize i)
    {
        switch (i)
        {
        case 0: return x;
        case 1: return y;
        case 2: return z;
        default: return w;
        }
    }
    /// Returns the value of component `i` (0 = x, 1 = y, 2 = z, 3 = w).
    ///
    /// - Param i: component index; any value above 3 returns `w` (not checked).
    f32 operator[](usize i) const
    {
        switch (i)
        {
        case 0: return x;
        case 1: return y;
        case 2: return z;
        default: return w;
        }
    }

    /// Returns the component-wise sum.
    Vec4 operator+(const Vec4& o) const { return Vec4(x + o.x, y + o.y, z + o.z, w + o.w); }
    /// Returns the component-wise difference.
    Vec4 operator-(const Vec4& o) const { return Vec4(x - o.x, y - o.y, z - o.z, w - o.w); }
    /// Returns the vector with every component multiplied by `s`.
    Vec4 operator*(f32 s) const { return Vec4(x * s, y * s, z * s, w * s); }

    /// Returns the dot product with `o`.
    f32 Dot(const Vec4& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }

    /// Returns the squared length; cheaper than `Length`.
    f32 LengthSquared() const { return Dot(*this); }
    /// Returns the Euclidean length.
    f32 Length() const { return Sqrt(LengthSquared()); }

    /// Returns the vector scaled to unit length.
    ///
    /// - Aborts: the length is zero.
    Vec4 Normalized() const
    {
        f32 len = Length();
        ENGINE_ASSERT(len > 0.0f, "Normalized() of a zero-length Vec4");
        return Vec4(x / len, y / len, z / len, w / len);
    }

    /// Returns true if every component compares exactly equal.
    bool operator==(const Vec4& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
    /// Returns true if any component differs.
    bool operator!=(const Vec4& o) const { return !(*this == o); }
};

}  // namespace vse
