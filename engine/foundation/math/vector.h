#pragma once

#include "lang/traits.h"
#include "log/log.h"
#include "math/math_util.h"
#include "types.h"

//! Vec2, Vec3 and Vec4: plain component structs with basic arithmetic, templates on the scalar type. `Vec2`, `Vec3` and
//! `Vec4` are the single-precision forms (`Vec3T<f32>`); `Vec2d`, `Vec3d` and `Vec4d` are the double-precision forms
//! (`Vec3T<f64>`), used for world positions.
//!
//! A single-precision vector widens to a double-precision one implicitly (the conversion is exact); the other way
//! is the named call `Narrow`, never an implicit conversion. No SIMD or special alignment. Index access uses an
//! explicit switch rather than relying on struct layout.
namespace vse
{

template <class S> struct Vec2T;
template <class S> struct Vec3T;
template <class S> struct Vec4T;

/// 2-component vector with members `x, y`, over the scalar `S` (`f32` or `f64`).
///
/// Components default to 0. Equality compares components exactly.
///
/// - Thread-safety: thread-compatible plain data.
template <class S> struct Vec2T
{
    /// X component.
    S x = S(0);
    /// Y component.
    S y = S(0);

    /// Creates the zero vector.
    constexpr Vec2T() = default;
    /// Creates a vector from its components.
    constexpr Vec2T(S x_, S y_) : x(x_), y(y_) {}
    /// Widens a single-precision vector (exact); only for the double-precision form.
    template <class T>
        requires(IsSame<S, f64> && IsSame<T, f32>)
    constexpr Vec2T(const Vec2T<T>& o) : x(o.x), y(o.y)
    {
    }
    /// Narrows to single precision (rounds each component to the nearest `f32`); only for the double-precision form.
    ///
    /// - Fails: never.
    constexpr Vec2T<f32> Narrow() const
        requires(IsSame<S, f64>)
    {
        return Vec2T<f32>(static_cast<f32>(x), static_cast<f32>(y));
    }

    /// Returns a reference to component `i` (0 = x, 1 = y).
    ///
    /// - Param i: component index; any value above 1 returns `y` (not checked).
    S& operator[](usize i) { return i == 0 ? x : y; }
    /// Returns the value of component `i` (0 = x, 1 = y).
    ///
    /// - Param i: component index; any value above 1 returns `y` (not checked).
    S operator[](usize i) const { return i == 0 ? x : y; }

    /// Returns the component-wise sum.
    Vec2T operator+(const Vec2T& o) const { return Vec2T(x + o.x, y + o.y); }
    /// Returns the component-wise difference.
    Vec2T operator-(const Vec2T& o) const { return Vec2T(x - o.x, y - o.y); }
    /// Returns the vector with every component multiplied by `s`.
    Vec2T operator*(S s) const { return Vec2T(x * s, y * s); }

    /// Returns the dot product with `o`.
    S Dot(const Vec2T& o) const { return x * o.x + y * o.y; }

    /// Returns the squared length; cheaper than `Length`.
    S LengthSquared() const { return Dot(*this); }
    /// Returns the Euclidean length.
    S Length() const { return Sqrt(LengthSquared()); }

    /// Returns the vector scaled to unit length.
    ///
    /// - Aborts: the length is zero.
    Vec2T Normalized() const
    {
        S len = Length();
        ENGINE_ASSERT(len > S(0), "Normalized() of a zero-length Vec2");
        return Vec2T(x / len, y / len);
    }

    /// Returns true if every component compares exactly equal.
    bool operator==(const Vec2T& o) const { return x == o.x && y == o.y; }
    /// Returns true if any component differs.
    bool operator!=(const Vec2T& o) const { return !(*this == o); }
};

/// 3-component vector with members `x, y, z`, over the scalar `S` (`f32` or `f64`).
///
/// Components default to 0. Equality compares components exactly.
///
/// - Thread-safety: thread-compatible plain data.
template <class S> struct Vec3T
{
    /// X component.
    S x = S(0);
    /// Y component.
    S y = S(0);
    /// Z component.
    S z = S(0);

    /// Creates the zero vector.
    constexpr Vec3T() = default;
    /// Creates a vector from its components.
    constexpr Vec3T(S x_, S y_, S z_) : x(x_), y(y_), z(z_) {}
    /// Widens a single-precision vector (exact); only for the double-precision form.
    template <class T>
        requires(IsSame<S, f64> && IsSame<T, f32>)
    constexpr Vec3T(const Vec3T<T>& o) : x(o.x), y(o.y), z(o.z)
    {
    }
    /// Narrows to single precision (rounds each component to the nearest `f32`); only for the double-precision form.
    ///
    /// - Fails: never.
    constexpr Vec3T<f32> Narrow() const
        requires(IsSame<S, f64>)
    {
        return Vec3T<f32>(static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(z));
    }

    /// Returns a reference to component `i` (0 = x, 1 = y, 2 = z).
    ///
    /// - Param i: component index; any value above 2 returns `z` (not checked).
    S& operator[](usize i)
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
    S operator[](usize i) const
    {
        switch (i)
        {
        case 0: return x;
        case 1: return y;
        default: return z;
        }
    }

    /// Returns the component-wise sum.
    Vec3T operator+(const Vec3T& o) const { return Vec3T(x + o.x, y + o.y, z + o.z); }
    /// Returns the component-wise difference.
    Vec3T operator-(const Vec3T& o) const { return Vec3T(x - o.x, y - o.y, z - o.z); }
    /// Returns the vector with every component multiplied by `s`.
    Vec3T operator*(S s) const { return Vec3T(x * s, y * s, z * s); }

    /// Returns the dot product with `o`.
    S Dot(const Vec3T& o) const { return x * o.x + y * o.y + z * o.z; }

    /// Returns the cross product `this x o` (right-handed).
    Vec3T Cross(const Vec3T& o) const { return Vec3T(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x); }

    /// Returns the squared length; cheaper than `Length`.
    S LengthSquared() const { return Dot(*this); }
    /// Returns the Euclidean length.
    S Length() const { return Sqrt(LengthSquared()); }

    /// Returns the vector scaled to unit length.
    ///
    /// - Aborts: the length is zero.
    Vec3T Normalized() const
    {
        S len = Length();
        ENGINE_ASSERT(len > S(0), "Normalized() of a zero-length Vec3");
        return Vec3T(x / len, y / len, z / len);
    }

    /// Returns true if every component compares exactly equal.
    bool operator==(const Vec3T& o) const { return x == o.x && y == o.y && z == o.z; }
    /// Returns true if any component differs.
    bool operator!=(const Vec3T& o) const { return !(*this == o); }
};

/// 4-component vector with members `x, y, z, w`, over the scalar `S` (`f32` or `f64`).
///
/// Components default to 0. Equality compares components exactly.
///
/// - Thread-safety: thread-compatible plain data.
template <class S> struct Vec4T
{
    /// X component.
    S x = S(0);
    /// Y component.
    S y = S(0);
    /// Z component.
    S z = S(0);
    /// W component.
    S w = S(0);

    /// Creates the zero vector.
    constexpr Vec4T() = default;
    /// Creates a vector from its components.
    constexpr Vec4T(S x_, S y_, S z_, S w_) : x(x_), y(y_), z(z_), w(w_) {}
    /// Creates a vector from a Vec3 and a `w` component.
    constexpr Vec4T(const Vec3T<S>& v, S w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    /// Widens a single-precision vector (exact); only for the double-precision form.
    template <class T>
        requires(IsSame<S, f64> && IsSame<T, f32>)
    constexpr Vec4T(const Vec4T<T>& o) : x(o.x), y(o.y), z(o.z), w(o.w)
    {
    }
    /// Narrows to single precision (rounds each component to the nearest `f32`); only for the double-precision form.
    ///
    /// - Fails: never.
    constexpr Vec4T<f32> Narrow() const
        requires(IsSame<S, f64>)
    {
        return Vec4T<f32>(static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(z), static_cast<f32>(w));
    }

    /// Returns a reference to component `i` (0 = x, 1 = y, 2 = z, 3 = w).
    ///
    /// - Param i: component index; any value above 3 returns `w` (not checked).
    S& operator[](usize i)
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
    S operator[](usize i) const
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
    Vec4T operator+(const Vec4T& o) const { return Vec4T(x + o.x, y + o.y, z + o.z, w + o.w); }
    /// Returns the component-wise difference.
    Vec4T operator-(const Vec4T& o) const { return Vec4T(x - o.x, y - o.y, z - o.z, w - o.w); }
    /// Returns the vector with every component multiplied by `s`.
    Vec4T operator*(S s) const { return Vec4T(x * s, y * s, z * s, w * s); }

    /// Returns the dot product with `o`.
    S Dot(const Vec4T& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }

    /// Returns the squared length; cheaper than `Length`.
    S LengthSquared() const { return Dot(*this); }
    /// Returns the Euclidean length.
    S Length() const { return Sqrt(LengthSquared()); }

    /// Returns the vector scaled to unit length.
    ///
    /// - Aborts: the length is zero.
    Vec4T Normalized() const
    {
        S len = Length();
        ENGINE_ASSERT(len > S(0), "Normalized() of a zero-length Vec4");
        return Vec4T(x / len, y / len, z / len, w / len);
    }

    /// Returns true if every component compares exactly equal.
    bool operator==(const Vec4T& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
    /// Returns true if any component differs.
    bool operator!=(const Vec4T& o) const { return !(*this == o); }
};

/// The single-precision 2-component vector.
using Vec2 = Vec2T<f32>;
/// The single-precision 3-component vector: directions, normals, extents, local offsets, everything that is not a
/// world position.
using Vec3 = Vec3T<f32>;
/// The single-precision 4-component vector.
using Vec4 = Vec4T<f32>;
/// The double-precision 2-component vector.
using Vec2d = Vec2T<f64>;
/// The double-precision 3-component vector: world positions.
using Vec3d = Vec3T<f64>;
/// The double-precision 4-component vector.
using Vec4d = Vec4T<f64>;

}  // namespace vse
