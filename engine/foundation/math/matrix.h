#pragma once

#include "types.h"
#include "math/vector.h"

//! Mat3 and Mat4: column-major storage with the column-vector convention.
//!
//! Each matrix stores its columns directly. A vector transforms as `v' = M * v`,
//! and composition is left-multiplication: `M2 * M1` applies `M1` first. This
//! matches the GLM/OpenGL convention.
namespace vse
{

/// 3x3 float matrix stored as three column vectors.
///
/// The default-constructed matrix has all-zero columns; use `Identity` for the
/// identity.
///
/// - Thread-safety: thread-compatible plain data.
struct Mat3
{
    /// The columns; `col[j][i]` is the element in row `i`, column `j`.
    Vec3 col[3];

    /// Returns the identity matrix.
    static Mat3 Identity()
    {
        Mat3 m;
        m.col[0] = Vec3(1, 0, 0);
        m.col[1] = Vec3(0, 1, 0);
        m.col[2] = Vec3(0, 0, 1);
        return m;
    }

    /// Builds a matrix whose columns are the three given axes.
    ///
    /// - Param x_axis: first column.
    /// - Param y_axis: second column.
    /// - Param z_axis: third column.
    static Mat3 FromBasis(const Vec3& x_axis, const Vec3& y_axis, const Vec3& z_axis)
    {
        Mat3 m;
        m.col[0] = x_axis;
        m.col[1] = y_axis;
        m.col[2] = z_axis;
        return m;
    }

    /// Returns the transposed matrix.
    Mat3 Transpose() const
    {
        Mat3 result;
        for (usize i = 0; i < 3; ++i)
        {
            for (usize j = 0; j < 3; ++j)
            {
                result.col[i][j] = col[j][i];
            }
        }
        return result;
    }
};

/// Returns `m` applied to column vector `v`.
inline Vec3 operator*(const Mat3& m, const Vec3& v)
{
    return m.col[0] * v.x + m.col[1] * v.y + m.col[2] * v.z;
}

/// Returns the matrix product `a * b`, which applies `b` first and then `a`.
inline Mat3 operator*(const Mat3& a, const Mat3& b)
{
    return Mat3::FromBasis(a * b.col[0], a * b.col[1], a * b.col[2]);
}

/// 4x4 float matrix stored as four column vectors, for homogeneous transforms.
///
/// The default-constructed matrix has all-zero columns; use `Identity` for the
/// identity.
///
/// - Thread-safety: thread-compatible plain data.
struct Mat4
{
    /// The columns; `col[j][i]` is the element in row `i`, column `j`.
    Vec4 col[4];

    /// Returns the identity matrix.
    static Mat4 Identity()
    {
        Mat4 m;
        m.col[0] = Vec4(1, 0, 0, 0);
        m.col[1] = Vec4(0, 1, 0, 0);
        m.col[2] = Vec4(0, 0, 1, 0);
        m.col[3] = Vec4(0, 0, 0, 1);
        return m;
    }

    /// Builds an affine matrix from three axes (w = 0) and a translation (w = 1).
    ///
    /// - Param x_axis: first column.
    /// - Param y_axis: second column.
    /// - Param z_axis: third column.
    /// - Param translation: fourth column.
    static Mat4 FromBasis(const Vec3& x_axis, const Vec3& y_axis, const Vec3& z_axis, const Vec3& translation)
    {
        Mat4 m;
        m.col[0] = Vec4(x_axis, 0);
        m.col[1] = Vec4(y_axis, 0);
        m.col[2] = Vec4(z_axis, 0);
        m.col[3] = Vec4(translation, 1);
        return m;
    }

    /// Builds a matrix directly from four columns.
    ///
    /// - Param c0: first column.
    /// - Param c1: second column.
    /// - Param c2: third column.
    /// - Param c3: fourth column.
    static Mat4 FromColumns(const Vec4& c0, const Vec4& c1, const Vec4& c2, const Vec4& c3)
    {
        Mat4 m;
        m.col[0] = c0;
        m.col[1] = c1;
        m.col[2] = c2;
        m.col[3] = c3;
        return m;
    }

    /// Returns the transposed matrix.
    Mat4 Transpose() const
    {
        Mat4 result;
        for (usize i = 0; i < 4; ++i)
        {
            for (usize j = 0; j < 4; ++j)
            {
                result.col[i][j] = col[j][i];
            }
        }
        return result;
    }
};

/// Returns `m` applied to column vector `v`.
inline Vec4 operator*(const Mat4& m, const Vec4& v)
{
    return m.col[0] * v.x + m.col[1] * v.y + m.col[2] * v.z + m.col[3] * v.w;
}

/// Returns the matrix product `a * b`, which applies `b` first and then `a`.
inline Mat4 operator*(const Mat4& a, const Mat4& b)
{
    return Mat4::FromColumns(a * b.col[0], a * b.col[1], a * b.col[2], a * b.col[3]);
}

}  // namespace vse
