#include "physics/collide.h"

#include "lang/scalar.h"
#include "physics/collide_math.h"
#include "physics/contact.h"

namespace engine_placeholder::physics
{

namespace
{

using namespace collide_math;

// Two balls: the contact between a ball of radius `ra` at `ca` (shape A) and one of radius `rb` at `cb`, when they are
// within the margin.
bool BallBall(const Vec3& ca, f32 ra, const Vec3& cb, f32 rb, f32 margin, u32 feature, ContactManifold* out)
{
    const Vec3 d = cb - ca;
    const f32 d2 = d.LengthSquared();
    const f32 reach = ra + rb + margin;
    if (d2 > reach * reach)
    {
        return false;
    }
    Vec3 normal;
    f32 distance;
    if (d2 > kTiny)
    {
        distance = Sqrt(d2);
        normal = d * (1.0f / distance);
    }
    else
    {
        distance = 0.0f;
        normal = Vec3(0.0f, 1.0f, 0.0f);
    }
    out->normal = normal;
    out->count = 1;
    out->points[0].position_a = ca + normal * ra;
    out->points[0].position_b = cb - normal * rb;
    out->points[0].depth = ra + rb - distance;
    out->points[0].feature_id = feature;
    return true;
}

bool SphereSphere(const Shape& a, const Pose& pa, const Shape& b, const Pose& pb, f32 margin, ContactManifold* out)
{
    return BallBall(pa.position, a.radius, pb.position, b.radius, margin, 0, out);
}

// A is a sphere, B a capsule: the capsule is the ball of its radius at the closest point of its segment to the sphere's
// centre.
bool SphereCapsule(const Shape& a, const Pose& pa, const Shape& b, const Pose& pb, f32 margin, ContactManifold* out)
{
    const Vec3 on_axis = ClosestOnSegment(CapsuleSegment(b, pb), pa.position, nullptr);
    return BallBall(pa.position, a.radius, on_axis, b.radius, margin, 0, out);
}

bool CapsuleCapsule(const Shape& a, const Pose& pa, const Shape& b, const Pose& pb, f32 margin, ContactManifold* out)
{
    const Segment sa = CapsuleSegment(a, pa);
    const Segment sb = CapsuleSegment(b, pb);
    const Vec3 da = sa.q - sa.p;
    const Vec3 db = sb.q - sb.p;
    const f32 la2 = da.LengthSquared();
    const f32 lb2 = db.LengthSquared();
    const f32 reach = a.radius + b.radius + margin;
    // Parallel segments (not points) touch along a stretch: two contact points keep a capsule lying on another from
    // rocking.
    if (la2 > kTiny && lb2 > kTiny)
    {
        const Vec3 cross = da.Cross(db);
        if (cross.LengthSquared() <= 1e-6f * la2 * lb2)
        {
            const f32 la = Sqrt(la2);
            const Vec3 axis = da * (1.0f / la);
            // B's end points along A's axis, measured from A's start.
            f32 u0 = (sb.p - sa.p).Dot(axis);
            f32 u1 = (sb.q - sa.p).Dot(axis);
            if (u0 > u1)
            {
                const f32 swap = u0;
                u0 = u1;
                u1 = swap;
            }
            const f32 lo = FMax(0.0f, u0);
            const f32 hi = FMin(la, u1);
            if (hi - lo > 1e-4f)
            {
                const Vec3 mid_a = sa.p + axis * (0.5f * (lo + hi));
                const Vec3 mid_b = ClosestOnSegment(sb, mid_a, nullptr);
                const Vec3 gap = mid_b - mid_a;
                const f32 g2 = gap.LengthSquared();
                if (g2 > reach * reach)
                {
                    return false;
                }
                const Vec3 normal = g2 > kTiny ? gap * (1.0f / Sqrt(g2)) : Perpendicular(axis);
                out->normal = normal;
                out->count = 2;
                const f32 ends[2] = {lo, hi};
                for (u32 i = 0; i < 2; ++i)
                {
                    const Vec3 on_a = sa.p + axis * ends[i];
                    // The point of B's axis opposite this end of the shared stretch (the axes are parallel only to a
                    // tolerance, so each end is measured).
                    const Vec3 on_b = ClosestOnSegment(sb, on_a, nullptr);
                    out->points[i].position_a = on_a + normal * a.radius;
                    out->points[i].position_b = on_b - normal * b.radius;
                    out->points[i].depth = a.radius + b.radius - (on_b - on_a).Dot(normal);
                    out->points[i].feature_id = i + 1;
                }
                return true;
            }
        }
    }
    Vec3 ca;
    Vec3 cb;
    ClosestSegments(sa, sb, &ca, &cb);
    if (!BallBall(ca, a.radius, cb, b.radius, margin, 0, out))
    {
        return false;
    }
    if ((cb - ca).LengthSquared() <= kTiny)
    {
        // Crossing axes: the ball normal is arbitrary; use the common perpendicular of the two axes when there is one.
        const Vec3 cross = da.Cross(db);
        if (cross.LengthSquared() > kTiny)
        {
            out->normal = cross * (1.0f / Sqrt(cross.LengthSquared()));
            out->points[0].position_a = ca + out->normal * a.radius;
            out->points[0].position_b = cb - out->normal * b.radius;
        }
    }
    return true;
}

// A is a sphere, B a box. The point of the box nearest the sphere's centre decides; a centre inside the box leaves
// through the nearest face.
bool SphereBox(const Shape& a, const Pose& pa, const Shape& b, const Pose& pb, f32 margin, ContactManifold* out)
{
    const Mat3 rb = pb.rotation.ToMat3();
    const Vec3 rel = pa.position - pb.position;
    // The sphere's centre in the box's frame (R^T rel).
    const Vec3 local(rb.col[0].Dot(rel), rb.col[1].Dot(rel), rb.col[2].Dot(rel));
    const Vec3 h = b.half_extents;
    const Vec3 clamped(Clamp(local.x, -h.x, h.x), Clamp(local.y, -h.y, h.y), Clamp(local.z, -h.z, h.z));
    const Vec3 delta = local - clamped;
    const f32 d2 = delta.LengthSquared();
    const f32 r = a.radius;
    const f32 reach = r + margin;
    Vec3 n_local;  // from the box toward the sphere
    Vec3 q_local;  // the point of the box's surface
    f32 distance;
    u32 feature;
    if (d2 > kTiny)
    {
        if (d2 > reach * reach)
        {
            return false;
        }
        distance = Sqrt(d2);
        n_local = delta * (1.0f / distance);
        q_local = clamped;
        // The region: for each axis, 0 inside the slab, 1 beyond the minimum, 2 beyond the maximum (a face, an edge or
        // a vertex).
        const u32 rx = local.x < -h.x ? 1u : (local.x > h.x ? 2u : 0u);
        const u32 ry = local.y < -h.y ? 1u : (local.y > h.y ? 2u : 0u);
        const u32 rz = local.z < -h.z ? 1u : (local.z > h.z ? 2u : 0u);
        feature = 1u + rx + 3u * ry + 9u * rz;
    }
    else
    {
        // The centre is inside (or on the surface): leave through the face with the least penetration.
        const f32 px = h.x - Abs(local.x);
        const f32 py = h.y - Abs(local.y);
        const f32 pz = h.z - Abs(local.z);
        q_local = local;
        n_local = Vec3(0.0f, 0.0f, 0.0f);
        f32 pen;
        if (px <= py && px <= pz)
        {
            const f32 sign = local.x < 0.0f ? -1.0f : 1.0f;
            n_local.x = sign;
            q_local.x = sign * h.x;
            pen = px;
            feature = 28u + (local.x < 0.0f ? 0u : 1u);
        }
        else if (py <= pz)
        {
            const f32 sign = local.y < 0.0f ? -1.0f : 1.0f;
            n_local.y = sign;
            q_local.y = sign * h.y;
            pen = py;
            feature = 30u + (local.y < 0.0f ? 0u : 1u);
        }
        else
        {
            const f32 sign = local.z < 0.0f ? -1.0f : 1.0f;
            n_local.z = sign;
            q_local.z = sign * h.z;
            pen = pz;
            feature = 32u + (local.z < 0.0f ? 0u : 1u);
        }
        distance = -pen;  // negative: the centre is below the surface
    }
    const Vec3 n_world = rb * n_local;  // box toward sphere
    const Vec3 q_world = pb.position + rb * q_local;
    out->normal = Negate(n_world);  // A (the sphere) toward B (the box)
    out->count = 1;
    out->points[0].position_a = pa.position - n_world * r;
    out->points[0].position_b = q_world;
    out->points[0].depth = r - distance;
    out->points[0].feature_id = feature;
    return true;
}

}  // namespace

CollideResult Collide(const Shape& a, const Pose& pose_a, const Shape& b, const Pose& pose_b, f32 margin,
                      ContactManifold* manifold, const HullLibrary* hulls)
{
    if (a.Validate() != Validation::Ok || b.Validate() != Validation::Ok || !IsFinite(margin) || margin < 0.0f)
    {
        return CollideResult::None;
    }
    if (a.kind == ShapeKind::Hull || b.kind == ShapeKind::Hull)
    {
        return ConvexPair(a, pose_a, b, pose_b, margin, hulls, manifold) ? CollideResult::Contact : CollideResult::None;
    }
    // The table is kept for A <= B in the order sphere, box, capsule... by kind number; the other order is the same
    // test with the result flipped.
    const u8 ka = static_cast<u8>(a.kind);
    const u8 kb = static_cast<u8>(b.kind);
    if (ka > kb)
    {
        const CollideResult result = Collide(b, pose_b, a, pose_a, margin, manifold, hulls);
        if (result == CollideResult::Contact)
        {
            manifold->Flip();
        }
        return result;
    }
    bool hit = false;
    if (a.kind == ShapeKind::Sphere && b.kind == ShapeKind::Sphere)
    {
        hit = SphereSphere(a, pose_a, b, pose_b, margin, manifold);
    }
    else if (a.kind == ShapeKind::Sphere && b.kind == ShapeKind::Box)
    {
        hit = SphereBox(a, pose_a, b, pose_b, margin, manifold);
    }
    else if (a.kind == ShapeKind::Sphere && b.kind == ShapeKind::Capsule)
    {
        hit = SphereCapsule(a, pose_a, b, pose_b, margin, manifold);
    }
    else if (a.kind == ShapeKind::Capsule && b.kind == ShapeKind::Capsule)
    {
        hit = CapsuleCapsule(a, pose_a, b, pose_b, margin, manifold);
    }
    else if (a.kind == ShapeKind::Box && b.kind == ShapeKind::Box)
    {
        hit = BoxBox(a, pose_a, b, pose_b, margin, manifold);
    }
    else if (a.kind == ShapeKind::Box && b.kind == ShapeKind::Capsule)
    {
        hit = BoxCapsule(a, pose_a, b, pose_b, margin, manifold);
    }
    else
    {
        return CollideResult::Unsupported;
    }
    return hit ? CollideResult::Contact : CollideResult::None;
}

void CombineMaterials(const PhysicsMaterial& a, const PhysicsMaterial& b, ContactManifold* manifold)
{
    manifold->friction = Combine(CombineRuleFor(a.friction_rule, b.friction_rule), a.friction, b.friction);
    manifold->restitution = Combine(CombineRuleFor(a.restitution_rule, b.restitution_rule), a.restitution,
                                    b.restitution);
}

}  // namespace engine_placeholder::physics
