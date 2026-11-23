// Small geometry helpers shared by the narrow phase's files (Phase 25 Days 391-392). Internal: not part of the
// documented API.
#pragma once

#include "lang/scalar.h"
#include "containers/array.h"
#include "math/matrix.h"
#include "math/vector.h"
#include "physics/compound.h"
#include "physics/hull.h"
#include "physics/height_field.h"
#include "physics/triangle_mesh.h"
#include "physics/shapes.h"
#include "physics/contact.h"
#include "types.h"

namespace vse::physics::collide_math
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

struct ClipVertex
{
    Vec3 p;
    u32 id = 0;
};

struct Candidate
{
    Vec3 p;
    f32 depth = 0.0f;
    u32 id = 0;
};

inline u32 MixId(u32 a, u32 b, u32 plane)
{
    const u32 h = (a * 5u + b * 3u + plane + 1u) * 2654435761u;
    return 16u + ((h >> 8) % 0xFFC0u);
}

// Keeps the part of the polygon with `Dot(p, n) <= offset`. `out` holds `capacity` vertices; a convex polygon needs one
// more than it had, and non-finite input (which breaks the convexity) is cut off at the capacity.
inline u32 ClipPolygon(const ClipVertex* in, u32 count, const Vec3& n, f32 offset, u32 plane, ClipVertex* out,
                       u32 capacity)
{
    u32 produced = 0;
    for (u32 i = 0; i < count; ++i)
    {
        const ClipVertex& cur = in[i];
        const ClipVertex& next = in[(i + 1) % count];
        const f32 dc = cur.p.Dot(n) - offset;
        const f32 dn = next.p.Dot(n) - offset;
        const bool cur_in = dc <= 0.0f;
        const bool next_in = dn <= 0.0f;
        if (cur_in && produced < capacity)
        {
            out[produced++] = cur;
        }
        if (cur_in != next_in && produced < capacity)
        {
            const f32 t = dc / (dc - dn);
            out[produced].p = cur.p + (next.p - cur.p) * t;
            out[produced].id = MixId(cur.id, next.id, plane);
            ++produced;
        }
    }
    return produced;
}

// At most four points: the deepest, the one farthest from it, then the two that span the most area on either side.
// Returns how many were picked (in `pick`).
inline u32 ReduceCandidates(const Candidate* cand, u32 kept, const Vec3& nr, Array<u32, 4>* pick_out)
{
    Array<u32, 4>& pick = *pick_out;
    u32 picked = 0;
    if (kept <= 4)
    {
        for (u32 v = 0; v < kept; ++v)
        {
            pick[v] = v;
        }
        picked = kept;
    }
    else
    {
        u32 i0 = 0;
        for (u32 v = 1; v < kept; ++v)
        {
            if (cand[v].depth > cand[i0].depth)
            {
                i0 = v;
            }
        }
        u32 i1 = i0 == 0 ? 1u : 0u;
        f32 far = -1.0f;
        for (u32 v = 0; v < kept; ++v)
        {
            const f32 l2 = (cand[v].p - cand[i0].p).LengthSquared();
            if (v != i0 && l2 > far)
            {
                far = l2;
                i1 = v;
            }
        }
        const Vec3 edge = cand[i1].p - cand[i0].p;
        u32 i2 = kept;
        u32 i3 = kept;
        f32 best_pos = 1e-9f;
        f32 best_neg = -1e-9f;
        for (u32 v = 0; v < kept; ++v)
        {
            if (v == i0 || v == i1)
            {
                continue;
            }
            const f32 area = edge.Cross(cand[v].p - cand[i0].p).Dot(nr);
            if (area > best_pos)
            {
                best_pos = area;
                i2 = v;
            }
            if (area < best_neg)
            {
                best_neg = area;
                i3 = v;
            }
        }
        pick[picked++] = i0;
        pick[picked++] = i1;
        if (i2 != kept)
        {
            pick[picked++] = i2;
        }
        if (i3 != kept)
        {
            pick[picked++] = i3;
        }
    }
    return picked;
}

// A contact found by one piece of a composite (a mesh triangle, a compound child), before the pieces are merged.
struct FoundContact
{
    Vec3 normal = Vec3(0.0f, 1.0f, 0.0f);
    Vec3 pa = Vec3(0.0f, 0.0f, 0.0f);
    Vec3 pb = Vec3(0.0f, 0.0f, 0.0f);
    f32 depth = 0.0f;
    u32 id = 0;
};

// The manifold of the pieces' contacts: the deepest sets the normal, contacts within 11 degrees of it join (a point
// reached twice is kept once), and more than four reduce as for boxes. False when there is nothing.
inline bool MergeContacts(const FoundContact* found, u32 count, ContactManifold* out)
{
    if (count == 0)
    {
        return false;
    }
    u32 best = 0;
    for (u32 k = 1; k < count; ++k)
    {
        if (found[k].depth > found[best].depth)
        {
            best = k;
        }
    }
    const Vec3 n = found[best].normal;
    Array<Candidate, 64> cand;
    u32 kept = 0;
    for (u32 k = 0; k < count && k < 64; ++k)
    {
        bool duplicate = false;
        for (u32 j = 0; j < kept && !duplicate; ++j)
        {
            duplicate = (cand[j].p - found[k].pa).LengthSquared() < 1e-8f;
        }
        if (found[k].normal.Dot(n) >= 0.98f && !duplicate)
        {
            cand[kept].p = found[k].pa;
            cand[kept].depth = (found[k].pa - found[k].pb).Dot(n);
            cand[kept].id = k;
            ++kept;
        }
    }
    Array<u32, 4> pick;
    const u32 picked = ReduceCandidates(cand.Data(), kept, n, &pick);
    out->normal = n;
    out->count = picked;
    for (u32 k = 0; k < picked; ++k)
    {
        const FoundContact& f = found[cand[pick[k]].id];
        out->points[k].position_a = f.pa;
        out->points[k].position_b = f.pb;
        out->points[k].depth = cand[pick[k]].depth;
        out->points[k].feature_id = f.id;
    }
    return picked != 0;
}

// The tests of Day 392 (collide_box.cpp): box-box and box-capsule, the box being shape A in both.
bool BoxBox(const Shape& a, const Pose& pa, const Shape& b, const Pose& pb, f32 margin, ContactManifold* out);
// Any pair with a convex hull (Day 393), by GJK and EPA; the pair may be in either order.
bool ConvexPair(const Shape& a, const Pose& pa, const Shape& b, const Pose& pb, f32 margin, const HullLibrary* hulls,
                ContactManifold* out);
// A static mesh against any other shape (Day 394), the mesh being shape A: the contacts of the triangles near B.
bool MeshPair(const Shape& mesh_shape, const Pose& mesh_pose, const Shape& other, const Pose& other_pose, f32 margin,
              const HullLibrary* hulls, const MeshLibrary* meshes, ContactManifold* out);
// A static height field against any other shape, the field being shape A: the contacts of the triangles of the cells
// near B.
bool HeightFieldPair(const Shape& field_shape, const Pose& field_pose, const Shape& other, const Pose& other_pose,
                     f32 margin, const HullLibrary* hulls, const HeightFieldLibrary* fields, ContactManifold* out);
// Compounds (Day 395): each child, or pair of children, collided on its own and merged; either shape may be a compound.
bool CompoundPair(const Shape& a, const Pose& pose_a, const Shape& b, const Pose& pose_b, f32 margin,
                  const HullLibrary* hulls, const MeshLibrary* meshes, const CompoundLibrary* compounds,
                  ContactManifold* out, const HeightFieldLibrary* fields);
bool BoxCapsule(const Shape& a, const Pose& pa, const Shape& b, const Pose& pb, f32 margin, ContactManifold* out);

}  // namespace vse::physics::collide_math
