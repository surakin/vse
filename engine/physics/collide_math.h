// Small geometry helpers shared by the narrow phase's files (Phase 25 Days 379-380). Internal: not part of the
// documented API.
#pragma once

#include "lang/scalar.h"
#include "math/matrix.h"
#include "math/vector.h"
#include "physics/shapes.h"
#include "physics/contact.h"
#include "types.h"

namespace engine_placeholder::physics::collide_math
{

constexpr f32 kTiny = 1e-12f;

inline Vec3 Negate(const Vec3& v)
{
    return Vec3(-v.x, -v.y, -v.z);
}

inline f32 Clamp(f32 value, f32 lo, f32 hi)
{
    return FMin(FMax(value, lo), hi);
}

// A unit vector perpendicular to a unit vector, chosen the same way every time.
inline Vec3 Perpendicular(const Vec3& unit)
{
    const Vec3 other = Abs(unit.x) < 0.57735f ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 1.0f, 0.0f);
    const Vec3 p = unit.Cross(other);
    return p * (1.0f / Sqrt(p.LengthSquared()));
}

struct Segment
{
    Vec3 p;
    Vec3 q;
};

inline Segment CapsuleSegment(const Shape& shape, const Pose& pose)
{
    const Vec3 axis = pose.rotation.ToMat3().col[1] * shape.half_height;
    return Segment{pose.position - axis, pose.position + axis};
}

inline Vec3 ClosestOnSegment(const Segment& s, const Vec3& point, f32* t_out)
{
    const Vec3 d = s.q - s.p;
    const f32 len2 = d.LengthSquared();
    f32 t = 0.0f;
    if (len2 > kTiny)
    {
        t = Clamp((point - s.p).Dot(d) / len2, 0.0f, 1.0f);
    }
    if (t_out != nullptr)
    {
        *t_out = t;
    }
    return s.p + d * t;
}

// The closest points of two segments (Ericson, Real-Time Collision Detection 5.1.9), degenerate segments included.
inline void ClosestSegments(const Segment& s1, const Segment& s2, Vec3* c1, Vec3* c2)
{
    const Vec3 d1 = s1.q - s1.p;
    const Vec3 d2 = s2.q - s2.p;
    const Vec3 r = s1.p - s2.p;
    const f32 a = d1.LengthSquared();
    const f32 e = d2.LengthSquared();
    const f32 f = d2.Dot(r);
    f32 s = 0.0f;
    f32 t = 0.0f;
    if (a <= kTiny && e <= kTiny)
    {
        *c1 = s1.p;
        *c2 = s2.p;
        return;
    }
    if (a <= kTiny)
    {
        t = Clamp(f / e, 0.0f, 1.0f);
    }
    else
    {
        const f32 c = d1.Dot(r);
        if (e <= kTiny)
        {
            s = Clamp(-c / a, 0.0f, 1.0f);
        }
        else
        {
            const f32 b = d1.Dot(d2);
            const f32 denom = a * e - b * b;
            s = denom > 1e-9f * a * e ? Clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f)
            {
                t = 0.0f;
                s = Clamp(-c / a, 0.0f, 1.0f);
            }
            else if (t > 1.0f)
            {
                t = 1.0f;
                s = Clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    *c1 = s1.p + d1 * s;
    *c2 = s2.p + d2 * t;
}

// The tests of Day 380 (collide_box.cpp): box-box and box-capsule, the box being shape A in both.
bool BoxBox(const Shape& a, const Pose& pa, const Shape& b, const Pose& pb, f32 margin, ContactManifold* out);
bool BoxCapsule(const Shape& a, const Pose& pa, const Shape& b, const Pose& pb, f32 margin, ContactManifold* out);

}  // namespace engine_placeholder::physics::collide_math
