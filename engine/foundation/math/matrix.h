#pragma once

#include "types.h"
#include "math/vector.h"

// Mat3/Mat4: column-major storage (each matrix holds its columns
// directly, so FromBasis is a trivial store), column-vector convention
// (v' = M * v; composing transforms is left-multiplication — M2 * M1
// applies M1 first, then M2). Matches the GLM/OpenGL/Vulkan-adjacent
// convention. Not fixed elsewhere in the plan — documented here since
// it's a load-bearing choice for later renderer/transform code.
namespace engine_placeholder {

struct Mat3 {
    Vec3 col[3];

    static Mat3 Identity() {
        Mat3 m;
        m.col[0] = Vec3(1, 0, 0);
        m.col[1] = Vec3(0, 1, 0);
        m.col[2] = Vec3(0, 0, 1);
        return m;
    }

    static Mat3 FromBasis(const Vec3& x_axis, const Vec3& y_axis, const Vec3& z_axis) {
        Mat3 m;
        m.col[0] = x_axis;
        m.col[1] = y_axis;
        m.col[2] = z_axis;
        return m;
    }

    Mat3 Transpose() const {
        Mat3 result;
        for (usize i = 0; i < 3; ++i) {
            for (usize j = 0; j < 3; ++j) {
                result.col[i][j] = col[j][i];
            }
        }
        return result;
    }
};

inline Vec3 operator*(const Mat3& m, const Vec3& v) { return m.col[0] * v.x + m.col[1] * v.y + m.col[2] * v.z; }

inline Mat3 operator*(const Mat3& a, const Mat3& b) {
    return Mat3::FromBasis(a * b.col[0], a * b.col[1], a * b.col[2]);
}

struct Mat4 {
    Vec4 col[4];

    static Mat4 Identity() {
        Mat4 m;
        m.col[0] = Vec4(1, 0, 0, 0);
        m.col[1] = Vec4(0, 1, 0, 0);
        m.col[2] = Vec4(0, 0, 1, 0);
        m.col[3] = Vec4(0, 0, 0, 1);
        return m;
    }

    static Mat4 FromBasis(const Vec3& x_axis, const Vec3& y_axis, const Vec3& z_axis, const Vec3& translation) {
        Mat4 m;
        m.col[0] = Vec4(x_axis, 0);
        m.col[1] = Vec4(y_axis, 0);
        m.col[2] = Vec4(z_axis, 0);
        m.col[3] = Vec4(translation, 1);
        return m;
    }

    static Mat4 FromColumns(const Vec4& c0, const Vec4& c1, const Vec4& c2, const Vec4& c3) {
        Mat4 m;
        m.col[0] = c0;
        m.col[1] = c1;
        m.col[2] = c2;
        m.col[3] = c3;
        return m;
    }

    Mat4 Transpose() const {
        Mat4 result;
        for (usize i = 0; i < 4; ++i) {
            for (usize j = 0; j < 4; ++j) {
                result.col[i][j] = col[j][i];
            }
        }
        return result;
    }
};

inline Vec4 operator*(const Mat4& m, const Vec4& v) {
    return m.col[0] * v.x + m.col[1] * v.y + m.col[2] * v.z + m.col[3] * v.w;
}

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    return Mat4::FromColumns(a * b.col[0], a * b.col[1], a * b.col[2], a * b.col[3]);
}

} // namespace engine_placeholder
