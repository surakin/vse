#include "renderer/frustum.h"

#include <cmath>

#include "log/assert.h"

namespace engine_placeholder::renderer {

namespace {

f32 Abs(f32 v) { return std::fabs(v); }

// Row `i` of a column-major matrix.
Vec4 Row(const Mat4& m, u32 i) { return Vec4(m.col[0][i], m.col[1][i], m.col[2][i], m.col[3][i]); }

Plane MakePlane(const Vec4& v) {
    const Vec3 n(v.x, v.y, v.z);
    const f32 len = n.Length();
    ENGINE_ASSERT(len > 0.0f && std::isfinite(len), "Frustum: the view-projection is degenerate (zero or not finite)");
    Plane p;
    p.normal = n * (1.0f / len);
    p.d = v.w / len;
    return p;
}

} // namespace

Frustum Frustum::FromViewProjection(const Mat4& vp) {
    // Gribb-Hartmann: with clip = vp * p, a point is inside when -w <= x <= w, -w <= y <= w and 0 <= z <= w (Vulkan depth).
    const Vec4 r0 = Row(vp, 0);
    const Vec4 r1 = Row(vp, 1);
    const Vec4 r2 = Row(vp, 2);
    const Vec4 r3 = Row(vp, 3);
    Frustum f;
    f.planes[0] = MakePlane(r3 + r0); // left:   w + x >= 0
    f.planes[1] = MakePlane(r3 - r0); // right:  w - x >= 0
    f.planes[2] = MakePlane(r3 + r1); // bottom: w + y >= 0
    f.planes[3] = MakePlane(r3 - r1); // top:    w - y >= 0
    f.planes[4] = MakePlane(r2);      // near:   z >= 0
    f.planes[5] = MakePlane(r3 - r2); // far:    w - z >= 0
    return f;
}

Containment Frustum::TestSphere(const BoundingSphere& s) const {
    bool straddles = false;
    for (const Plane& plane : planes) {
        const f32 distance = plane.Distance(s.center);
        if (distance < -s.radius) {
            return Containment::Outside;
        }
        if (distance < s.radius) {
            straddles = true;
        }
    }
    return straddles ? Containment::Intersects : Containment::Inside;
}

Containment Frustum::TestAabb(const Aabb& box) const {
    const Vec3 center = box.Center();
    const Vec3 half = box.HalfExtents();
    bool straddles = false;
    for (const Plane& plane : planes) {
        // The box's extent along the plane normal: the farthest corner is `reach` away from the center in each direction.
        const f32 reach = Abs(plane.normal.x) * half.x + Abs(plane.normal.y) * half.y + Abs(plane.normal.z) * half.z;
        const f32 distance = plane.Distance(center);
        if (distance < -reach) {
            return Containment::Outside;
        }
        if (distance < reach) {
            straddles = true;
        }
    }
    return straddles ? Containment::Intersects : Containment::Inside;
}

bool Frustum::Visible(const Bounds& bounds) const {
    switch (TestSphere(bounds.sphere)) {
    case Containment::Outside:
        return false;
    case Containment::Inside:
        return true;
    case Containment::Intersects:
        break;
    }
    return TestAabb(bounds.box) != Containment::Outside;
}

} // namespace engine_placeholder::renderer
