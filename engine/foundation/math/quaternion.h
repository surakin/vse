#pragma once

#include "log/log.h"
#include "math/math_util.h"
#include "math/matrix.h"
#include "math/vector.h"

//! Quaternion rotations: Hamilton convention, right-handed, `w` is the scalar part.
//!
//! Consistent with matrix.h: `ToMat3` rotates a vector the same way
//! `RotateVector` does, and `a * b` rotates by `b` first, then by `a`.
namespace engine_placeholder {

/// Rotation stored as a quaternion `(x, y, z, w)`, with `w` the scalar part.
///
/// Rotation functions expect unit quaternions; nothing renormalizes
/// automatically, so call `Normalized` after accumulating many products. The
/// default value is the identity rotation.
///
/// - Thread-safety: thread-compatible plain data.
struct Quaternion {
    /// X component of the vector part.
    f32 x = 0.0f;
    /// Y component of the vector part.
    f32 y = 0.0f;
    /// Z component of the vector part.
    f32 z = 0.0f;
    /// Scalar part.
    f32 w = 1.0f;

    /// Creates the identity rotation.
    constexpr Quaternion() = default;
    /// Creates a quaternion from raw components; not normalized.
    constexpr Quaternion(f32 x_, f32 y_, f32 z_, f32 w_) : x(x_), y(y_), z(z_), w(w_) {}

    /// Returns the identity rotation `(0, 0, 0, 1)`.
    static Quaternion Identity() { return Quaternion(0.0f, 0.0f, 0.0f, 1.0f); }

    /// Builds the rotation of `radians` about `axis`, counter-clockwise looking
    /// down the axis toward the origin (right-handed).
    ///
    /// - Param axis: rotation axis; need not be unit length.
    /// - Param radians: rotation angle in radians.
    /// - Aborts: `axis` has zero length.
    static Quaternion FromAxisAngle(const Vec3& axis, f32 radians) {
        Vec3 n = axis.Normalized();
        f32 half = radians * 0.5f;
        f32 s = Sin(half);
        return Quaternion(n.x * s, n.y * s, n.z * s, Cos(half));
    }

    /// Returns the squared magnitude of the four components.
    f32 LengthSquared() const { return x * x + y * y + z * z + w * w; }
    /// Returns the magnitude of the four components.
    f32 Length() const { return Sqrt(LengthSquared()); }

    /// Returns the quaternion scaled to unit magnitude.
    ///
    /// - Aborts: the magnitude is zero.
    Quaternion Normalized() const {
        f32 len = Length();
        ENGINE_ASSERT(len > 0.0f, "Normalized() of a zero-length Quaternion");
        f32 inv = 1.0f / len;
        return Quaternion(x * inv, y * inv, z * inv, w * inv);
    }

    /// Returns the conjugate `(-x, -y, -z, w)`, which is the inverse for a unit
    /// quaternion.
    Quaternion Conjugate() const { return Quaternion(-x, -y, -z, w); }

    /// Returns the multiplicative inverse (conjugate divided by squared magnitude).
    ///
    /// - Aborts: the magnitude is zero.
    Quaternion Inverse() const {
        f32 len_sq = LengthSquared();
        ENGINE_ASSERT(len_sq > 0.0f, "Inverse() of a zero-length Quaternion");
        f32 inv = 1.0f / len_sq;
        return Quaternion(-x * inv, -y * inv, -z * inv, w * inv);
    }

    /// Returns the four-component dot product with `o`.
    f32 Dot(const Quaternion& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }

    /// Rotates `v` by this quaternion, computing `q * (v, 0) * conjugate(q)`.
    ///
    /// - Param v: vector to rotate.
    /// - Returns: the rotated vector. Assumes a unit quaternion.
    Vec3 RotateVector(const Vec3& v) const {
        Vec3 qv(x, y, z);
        Vec3 t = qv.Cross(v) * 2.0f;
        return v + (t * w) + qv.Cross(t);
    }

    /// Converts to the equivalent 3x3 rotation matrix (column-vector convention).
    ///
    /// Assumes a unit quaternion.
    Mat3 ToMat3() const {
        f32 xx = x * x, yy = y * y, zz = z * z;
        f32 xy = x * y, xz = x * z, yz = y * z;
        f32 wx = w * x, wy = w * y, wz = w * z;
        return Mat3::FromBasis(Vec3(1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy)),
                                Vec3(2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx)),
                                Vec3(2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy)));
    }

    /// Converts to a 4x4 rotation matrix with zero translation. Assumes a unit quaternion.
    Mat4 ToMat4() const {
        Mat3 m3 = ToMat3();
        return Mat4::FromBasis(m3.col[0], m3.col[1], m3.col[2], Vec3(0.0f, 0.0f, 0.0f));
    }

    /// Converts a rotation matrix to a quaternion using Shepperd's method.
    ///
    /// The method picks one of four branches (trace positive, else the largest
    /// diagonal entry) so the divisor stays well away from zero for every
    /// rotation. The chosen component is not always the largest of w, x, y, z.
    ///
    /// - Param m: a proper rotation matrix (orthonormal, determinant +1);
    ///   other matrices give an unspecified result.
    /// - Returns: a unit quaternion; it may be the negation of the quaternion that
    ///   built `m`, which represents the same rotation.
    static Quaternion FromMat3(const Mat3& m) {
        f32 m00 = m.col[0].x, m01 = m.col[1].x, m02 = m.col[2].x;
        f32 m10 = m.col[0].y, m11 = m.col[1].y, m12 = m.col[2].y;
        f32 m20 = m.col[0].z, m21 = m.col[1].z, m22 = m.col[2].z;

        f32 trace = m00 + m11 + m22;
        if (trace > 0.0f) {
            f32 s = Sqrt(trace + 1.0f) * 2.0f; // s == 4 * w
            return Quaternion((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s);
        }
        if (m00 > m11 && m00 > m22) {
            f32 s = Sqrt(1.0f + m00 - m11 - m22) * 2.0f; // s == 4 * x
            return Quaternion(0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
        }
        if (m11 > m22) {
            f32 s = Sqrt(1.0f + m11 - m00 - m22) * 2.0f; // s == 4 * y
            return Quaternion((m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s);
        }
        f32 s = Sqrt(1.0f + m22 - m00 - m11) * 2.0f; // s == 4 * z
        return Quaternion((m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s);
    }
};

/// Returns the Hamilton product `a * b`, the rotation that applies `b` first and
/// then `a` (matching the `M2 * M1` order in matrix.h).
inline Quaternion operator*(const Quaternion& a, const Quaternion& b) {
    return Quaternion(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                       a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                       a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                       a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

/// Interpolates along the shortest arc between two unit quaternions at constant
/// angular speed.
///
/// If `a` and `b` are on opposite hemispheres, `b` is negated first so the short
/// way round is taken. Nearly parallel inputs fall back to a normalized linear
/// interpolation to avoid dividing by a near-zero sine.
///
/// - Param a: start rotation (result at `t` = 0); must be unit length.
/// - Param b: end rotation (result at `t` = 1); must be unit length.
/// - Param t: interpolation factor, normally in [0, 1]; values outside
///   extrapolate.
/// - Returns: the interpolated rotation.
inline Quaternion Slerp(const Quaternion& a, const Quaternion& b, f32 t) {
    f32 cos_theta = a.Dot(b);
    Quaternion b_adjusted = b;
    if (cos_theta < 0.0f) {
        cos_theta = -cos_theta;
        b_adjusted = Quaternion(-b.x, -b.y, -b.z, -b.w);
    }

    if (cos_theta > 0.9995f) {
        Quaternion lerped(a.x + (b_adjusted.x - a.x) * t, a.y + (b_adjusted.y - a.y) * t,
                           a.z + (b_adjusted.z - a.z) * t, a.w + (b_adjusted.w - a.w) * t);
        return lerped.Normalized();
    }

    f32 theta = Acos(cos_theta);
    f32 sin_theta = Sin(theta);
    f32 weight_a = Sin((1.0f - t) * theta) / sin_theta;
    f32 weight_b = Sin(t * theta) / sin_theta;
    return Quaternion(weight_a * a.x + weight_b * b_adjusted.x, weight_a * a.y + weight_b * b_adjusted.y,
                       weight_a * a.z + weight_b * b_adjusted.z, weight_a * a.w + weight_b * b_adjusted.w);
}

} // namespace engine_placeholder
