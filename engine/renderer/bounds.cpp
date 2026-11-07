#include "renderer/bounds.h"

#include <cstring>

#include "log/assert.h"
#include "math/math_util.h"

namespace engine_placeholder::renderer {

Aabb ComputeAabb(const void* points, u32 count, u32 stride_bytes) {
    ENGINE_ASSERT(points != nullptr && count > 0 && stride_bytes >= 12, "ComputeAabb: need at least one point of at least 12 bytes");
    const auto* bytes = static_cast<const byte*>(points);
    Aabb box;
    for (u32 i = 0; i < count; ++i) {
        f32 p[3];
        std::memcpy(p, bytes + static_cast<usize>(i) * stride_bytes, sizeof(p));
        if (i == 0) {
            box.min = box.max = Vec3(p[0], p[1], p[2]);
            continue;
        }
        for (u32 k = 0; k < 3; ++k) {
            if (p[k] < box.min[k]) box.min[k] = p[k];
            if (p[k] > box.max[k]) box.max[k] = p[k];
        }
    }
    return box;
}

Bounds ComputeBounds(const void* points, u32 count, u32 stride_bytes) {
    Bounds bounds;
    bounds.box = ComputeAabb(points, count, stride_bytes);
    bounds.sphere.center = bounds.box.Center();
    f32 farthest = 0.0f;
    const auto* bytes = static_cast<const byte*>(points);
    for (u32 i = 0; i < count; ++i) {
        f32 p[3];
        std::memcpy(p, bytes + static_cast<usize>(i) * stride_bytes, sizeof(p));
        const f32 d2 = (Vec3(p[0], p[1], p[2]) - bounds.sphere.center).LengthSquared();
        if (d2 > farthest) farthest = d2;
    }
    bounds.sphere.radius = Sqrt(farthest);
    return bounds;
}

Bounds TransformBounds(const Bounds& local, const Mat4& m) {
    Bounds out;
    // Box: transform the center, and spread the half extents by the absolute value of the upper 3x3 (Arvo's method).
    const Vec3 center = local.box.Center();
    const Vec3 half = local.box.HalfExtents();
    const Vec4 c = m * Vec4(center, 1.0f);
    Vec3 new_half(0, 0, 0);
    for (u32 row = 0; row < 3; ++row) {
        f32 sum = 0.0f;
        for (u32 col = 0; col < 3; ++col) {
            const f32 e = m.col[col][row];
            sum += (e < 0 ? -e : e) * half[col];
        }
        new_half[row] = sum;
    }
    out.box.min = Vec3(c.x, c.y, c.z) - new_half;
    out.box.max = Vec3(c.x, c.y, c.z) + new_half;

    const Vec4 sc = m * Vec4(local.sphere.center, 1.0f);
    f32 scale2 = 0.0f;
    for (u32 col = 0; col < 3; ++col) {
        const f32 l2 = Vec3(m.col[col].x, m.col[col].y, m.col[col].z).LengthSquared();
        if (l2 > scale2) scale2 = l2;
    }
    out.sphere.center = Vec3(sc.x, sc.y, sc.z);
    out.sphere.radius = local.sphere.radius * Sqrt(scale2);
    return out;
}

} // namespace engine_placeholder::renderer
