#include "physics/query.h"

#include "algorithm.h"
#include "lang/scalar.h"
#include "physics/collide.h"
#include "physics/compound.h"
#include "physics/triangle_mesh.h"

namespace engine_placeholder::physics
{

namespace
{

constexpr f32 kBig = 1e30f;

struct LocalHit
{
    f32 t = 0.0f;
    Vec3 normal = Vec3(0.0f, 1.0f, 0.0f);
    u32 triangle = kNoTriangle;
};

struct Libraries
{
    const HullLibrary* hulls;
    const MeshLibrary* meshes;
    const CompoundLibrary* compounds;
};

bool Finite(const Vec3& v)
{
    return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
}

// The ray against a sphere at `center`; `from_y` and `to_y` limit the hit point's y when set (the caps of a capsule).
bool RaySphere(const Vec3& o, const Vec3& d, f32 tmax, const Vec3& center, f32 r, LocalHit* out)
{
    const Vec3 m = o - center;
    const f32 b = m.Dot(d);
    const f32 c = m.LengthSquared() - r * r;
    if (c <= 0.0f)
    {
        out->t = 0.0f;
        out->normal = d * -1.0f;
        return true;
    }
    if (b > 0.0f)
    {
        return false;
    }
    const f32 disc = b * b - c;
    if (disc < 0.0f)
    {
        return false;
    }
    const f32 t = -b - Sqrt(disc);
    if (t > tmax)
    {
        return false;
    }
    out->t = t;
    out->normal = (m + d * t) * (1.0f / r);
    return true;
}

bool RayBox(const Vec3& o, const Vec3& d, f32 tmax, const Vec3& h, LocalHit* out)
{
    f32 tnear = -kBig;
    f32 tfar = kBig;
    Vec3 normal(0.0f, 1.0f, 0.0f);
    const f32 oo[3] = {o.x, o.y, o.z};
    const f32 dd[3] = {d.x, d.y, d.z};
    const f32 hh[3] = {h.x, h.y, h.z};
    for (u32 i = 0; i < 3; ++i)
    {
        if (Abs(dd[i]) < 1e-12f)
        {
            if (Abs(oo[i]) > hh[i])
            {
                return false;
            }
            continue;
        }
        const f32 inv = 1.0f / dd[i];
        f32 t1 = (-hh[i] - oo[i]) * inv;
        f32 t2 = (hh[i] - oo[i]) * inv;
        f32 sign = -1.0f;
        if (t1 > t2)
        {
            const f32 t = t1;
            t1 = t2;
            t2 = t;
            sign = 1.0f;
        }
        if (t1 > tnear)
        {
            tnear = t1;
            normal = Vec3(i == 0 ? sign : 0.0f, i == 1 ? sign : 0.0f, i == 2 ? sign : 0.0f);
        }
        if (t2 < tfar)
        {
            tfar = t2;
        }
        if (tnear > tfar)
        {
            return false;
        }
    }
    if (tfar < 0.0f)
    {
        return false;
    }
    if (tnear < 0.0f)
    {
        out->t = 0.0f;
        out->normal = d * -1.0f;
        return true;
    }
    if (tnear > tmax)
    {
        return false;
    }
    out->t = tnear;
    out->normal = normal;
    return true;
}

bool RayCapsule(const Vec3& o, const Vec3& d, f32 tmax, f32 half_height, f32 r, LocalHit* out)
{
    // Inside: the distance to the axis segment.
    const f32 cy = Clamp(o.y, -half_height, half_height);
    const Vec3 near_axis(0.0f, cy, 0.0f);
    if ((o - near_axis).LengthSquared() <= r * r)
    {
        out->t = 0.0f;
        out->normal = d * -1.0f;
        return true;
    }
    bool hit = false;
    LocalHit best;
    best.t = kBig;
    // The side: an infinite cylinder, cut to the segment.
    const f32 a = d.x * d.x + d.z * d.z;
    if (a > 1e-12f)
    {
        const f32 b = o.x * d.x + o.z * d.z;
        const f32 c = o.x * o.x + o.z * o.z - r * r;
        const f32 disc = b * b - a * c;
        if (disc >= 0.0f)
        {
            const f32 t = (-b - Sqrt(disc)) / a;
            const f32 y = o.y + t * d.y;
            if (t >= 0.0f && t <= tmax && Abs(y) <= half_height)
            {
                best.t = t;
                best.normal = Vec3(o.x + t * d.x, 0.0f, o.z + t * d.z) * (1.0f / r);
                hit = true;
            }
        }
    }
    for (u32 cap = 0; cap < 2; ++cap)
    {
        const f32 sign = cap == 0 ? 1.0f : -1.0f;
        LocalHit s;
        if (RaySphere(o, d, tmax, Vec3(0.0f, sign * half_height, 0.0f), r, &s) && s.t < best.t)
        {
            const f32 y = o.y + s.t * d.y;
            if (y * sign >= half_height)
            {
                best = s;
                hit = true;
            }
        }
    }
    if (hit)
    {
        *out = best;
    }
    return hit;
}

bool RayHull(const ConvexHull& hull, const Vec3& o, const Vec3& d, f32 tmax, LocalHit* out)
{
    f32 tnear = -kBig;
    f32 tfar = kBig;
    Vec3 normal(0.0f, 1.0f, 0.0f);
    for (u32 i = 0; i < hull.FaceCount(); ++i)
    {
        const HullFace& f = hull.Face(i);
        const f32 denom = f.normal.Dot(d);
        const f32 dist = f.offset - f.normal.Dot(o);  // positive inside
        if (Abs(denom) < 1e-12f)
        {
            if (dist < 0.0f)
            {
                return false;
            }
            continue;
        }
        const f32 t = dist / denom;
        if (denom < 0.0f)
        {
            if (t > tnear)
            {
                tnear = t;
                normal = f.normal;
            }
        }
        else if (t < tfar)
        {
            tfar = t;
        }
        if (tnear > tfar)
        {
            return false;
        }
    }
    if (tfar < 0.0f)
    {
        return false;
    }
    if (tnear < 0.0f)
    {
        out->t = 0.0f;
        out->normal = d * -1.0f;
        return true;
    }
    if (tnear > tmax)
    {
        return false;
    }
    out->t = tnear;
    out->normal = normal;
    return true;
}

bool RayTriangle(const Vec3& o, const Vec3& d, f32 tmax, const Vec3& a, const Vec3& b, const Vec3& c, f32* t_out)
{
    const Vec3 e1 = b - a;
    const Vec3 e2 = c - a;
    const Vec3 p = d.Cross(e2);
    const f32 det = e1.Dot(p);
    if (Abs(det) < 1e-12f)
    {
        return false;
    }
    const f32 inv = 1.0f / det;
    const Vec3 s = o - a;
    const f32 u = s.Dot(p) * inv;
    if (u < 0.0f || u > 1.0f)
    {
        return false;
    }
    const Vec3 q = s.Cross(e1);
    const f32 v = d.Dot(q) * inv;
    if (v < 0.0f || u + v > 1.0f)
    {
        return false;
    }
    const f32 t = e2.Dot(q) * inv;
    if (t < 0.0f || t > tmax)
    {
        return false;
    }
    *t_out = t;
    return true;
}

bool RayLocal(const Shape& shape, const Libraries& libs, const Vec3& o, const Vec3& d, f32 tmax, LocalHit* out)
{
    switch (shape.kind)
    {
    case ShapeKind::Sphere: return RaySphere(o, d, tmax, Vec3(0.0f, 0.0f, 0.0f), shape.radius, out);
    case ShapeKind::Box: return RayBox(o, d, tmax, shape.half_extents, out);
    case ShapeKind::Capsule: return RayCapsule(o, d, tmax, shape.half_height, shape.radius, out);
    case ShapeKind::Hull:
    {
        const ConvexHull* hull = libs.hulls != nullptr ? libs.hulls->Find(shape.hull) : nullptr;
        return hull != nullptr && RayHull(*hull, o, d, tmax, out);
    }
    case ShapeKind::Mesh:
    {
        const TriangleMesh* mesh = libs.meshes != nullptr ? libs.meshes->Find(shape.mesh) : nullptr;
        if (mesh == nullptr)
        {
            return false;
        }
        const Vec3 end = o + d * tmax;
        Aabb box;
        box.min = Vec3(Min(o.x, end.x), Min(o.y, end.y), Min(o.z, end.z));
        box.max = Vec3(Max(o.x, end.x), Max(o.y, end.y), Max(o.z, end.z));
        Vector<u32> candidates;
        mesh->QueryBox(box, &candidates);
        bool hit = false;
        for (usize i = 0; i < candidates.Size(); ++i)
        {
            const u32 tri = candidates[i];
            f32 t = 0.0f;
            if (RayTriangle(o, d, tmax, mesh->Vertex(mesh->Index(tri, 0)), mesh->Vertex(mesh->Index(tri, 1)),
                            mesh->Vertex(mesh->Index(tri, 2)), &t) &&
                (!hit || t < out->t))
            {
                hit = true;
                out->t = t;
                out->triangle = tri;
                const Vec3 n = mesh->Normal(tri);
                out->normal = n.Dot(d) > 0.0f ? n * -1.0f : n;
            }
        }
        return hit;
    }
    case ShapeKind::Compound:
    {
        const CompoundShape* compound = libs.compounds != nullptr ? libs.compounds->Find(shape.compound) : nullptr;
        if (compound == nullptr)
        {
            return false;
        }
        bool hit = false;
        for (u32 i = 0; i < compound->ChildCount(); ++i)
        {
            const CompoundChild& child = compound->Child(i);
            const Quaternion inv = child.rotation.Conjugate();
            LocalHit h;
            if (RayLocal(child.shape, libs, inv.RotateVector(o - child.offset), inv.RotateVector(d), tmax, &h) &&
                (!hit || h.t < out->t))
            {
                hit = true;
                *out = h;
                out->normal = child.rotation.RotateVector(h.normal);
            }
        }
        return hit;
    }
    }
    return false;
}

bool Passes(const PhysicsQueryFilter& filter, const ColliderRecord& record, const Body& body)
{
    const Collider& c = record.value;
    if (((filter.layer_mask >> c.layer) & 1u) == 0 || (c.is_trigger && !filter.include_triggers) ||
        !GroupsCollide(filter.group, filter.mask, c.group, c.mask))
    {
        return false;
    }
    switch (body.settings.type)
    {
    case BodyType::Static:
        if (!filter.include_static)
        {
            return false;
        }
        break;
    case BodyType::Kinematic:
        if (!filter.include_kinematic)
        {
            return false;
        }
        break;
    case BodyType::Dynamic:
        if (!filter.include_dynamic)
        {
            return false;
        }
        break;
    }
    for (usize i = 0; i < filter.ignore.Size(); ++i)
    {
        if (filter.ignore[i] == record.owner)
        {
            return false;
        }
    }
    return true;
}

Libraries LibrariesOf(const PhysicsWorld& world)
{
    return Libraries{&world.Hulls(), &world.Meshes(), &world.Compounds()};
}

Pose PoseOf(const Body& body, const Collider& collider)
{
    Pose pose;
    pose.position = body.position + body.orientation.ToMat3() * collider.offset;
    pose.rotation = body.orientation * collider.rotation;
    return pose;
}

bool Unit(const Vec3& v, Vec3* out)
{
    const f32 n2 = v.LengthSquared();
    if (!Finite(v) || !(n2 > 1e-20f))
    {
        return false;
    }
    *out = v * (1.0f / Sqrt(n2));
    return true;
}

// Every collider that passes the filter, in slot order, with its record and body.
template <class F> void ForEachCollider(const PhysicsWorld& world, const PhysicsQueryFilter& filter, F&& f)
{
    const u32 slots = world.Stats().collider_slots;
    for (u32 slot = 0; slot < slots; ++slot)
    {
        const ColliderId id = world.ColliderAtSlot(slot);
        if (!id.IsValid())
        {
            continue;
        }
        const ColliderRecord* record = world.FindCollider(id);
        const Body* body = record != nullptr ? world.Find(record->owner) : nullptr;
        if (body != nullptr && Passes(filter, *record, *body))
        {
            f(id, *record, *body);
        }
    }
}

bool BoxOverlapsRay(const Aabb& box, const Vec3& o, const Vec3& d, f32 tmax)
{
    f32 t0 = 0.0f;
    f32 t1 = tmax;
    const f32 oo[3] = {o.x, o.y, o.z};
    const f32 dd[3] = {d.x, d.y, d.z};
    const f32 lo[3] = {box.min.x, box.min.y, box.min.z};
    const f32 hi[3] = {box.max.x, box.max.y, box.max.z};
    for (u32 i = 0; i < 3; ++i)
    {
        if (Abs(dd[i]) < 1e-12f)
        {
            if (oo[i] < lo[i] || oo[i] > hi[i])
            {
                return false;
            }
            continue;
        }
        f32 a = (lo[i] - oo[i]) / dd[i];
        f32 b = (hi[i] - oo[i]) / dd[i];
        if (a > b)
        {
            const f32 t = a;
            a = b;
            b = t;
        }
        t0 = Max(t0, a);
        t1 = Min(t1, b);
        if (t0 > t1)
        {
            return false;
        }
    }
    return true;
}

// One ray against one collider; fills a hit in the world.
bool RayOne(const Libraries& libs, ColliderId id, const ColliderRecord& record, const Body& body, const Vec3& o,
            const Vec3& d, f32 tmax, PhysicsHit* hit)
{
    const Pose pose = PoseOf(body, record.value);
    if (!BoxOverlapsRay(record.value.shape.Bounds(pose.position, pose.rotation), o, d, tmax))
    {
        return false;
    }
    const Quaternion inv = pose.rotation.Conjugate();
    LocalHit local;
    if (!RayLocal(record.value.shape, libs, inv.RotateVector(o - pose.position), inv.RotateVector(d), tmax, &local))
    {
        return false;
    }
    hit->collider = id;
    hit->body = record.owner;
    hit->distance = local.t;
    hit->point = o + d * local.t;
    hit->normal = pose.rotation.RotateVector(local.normal);
    hit->triangle = local.triangle;
    return true;
}

}  // namespace

bool RayCast(const PhysicsWorld& world, const Vec3& origin, const Vec3& direction, f32 max_distance,
             const PhysicsQueryFilter& filter, PhysicsHit* hit)
{
    Vec3 d;
    if (!Finite(origin) || !Unit(direction, &d) || !IsFinite(max_distance) || !(max_distance > 0.0f))
    {
        return false;
    }
    const Libraries libs = LibrariesOf(world);
    bool found = false;
    PhysicsHit best;
    f32 reach = max_distance;  // shrinks to the closest hit so far: later colliders only count if nearer
    ForEachCollider(world, filter,
                    [&](ColliderId id, const ColliderRecord& record, const Body& body)
                    {
                        PhysicsHit h;
                        if (RayOne(libs, id, record, body, origin, d, reach, &h) &&
                            (!found || h.distance < best.distance ||
                             (h.distance == best.distance && id.ToKey() < best.collider.ToKey())))
                        {
                            best = h;
                            found = true;
                            reach = h.distance;
                        }
                    });
    if (found && hit != nullptr)
    {
        *hit = best;
    }
    return found;
}

usize RayCastAll(const PhysicsWorld& world, const Vec3& origin, const Vec3& direction, f32 max_distance,
                 const PhysicsQueryFilter& filter, Vector<PhysicsHit>* hits)
{
    hits->Clear();
    Vec3 d;
    if (!Finite(origin) || !Unit(direction, &d) || !IsFinite(max_distance) || !(max_distance > 0.0f))
    {
        return 0;
    }
    const Libraries libs = LibrariesOf(world);
    ForEachCollider(world, filter,
                    [&](ColliderId id, const ColliderRecord& record, const Body& body)
                    {
                        PhysicsHit h;
                        if (RayOne(libs, id, record, body, origin, d, max_distance, &h))
                        {
                            hits->Push(h);
                        }
                    });
    Sort(Span<PhysicsHit>(hits->Data(), hits->Size()), [](const PhysicsHit& a, const PhysicsHit& b)
         { return a.distance < b.distance || (a.distance == b.distance && a.collider.ToKey() < b.collider.ToKey()); });
    return hits->Size();
}

bool ShapeCast(const PhysicsWorld& world, const Shape& shape, const Vec3& position, const Quaternion& rotation,
               const Vec3& direction, f32 distance, const PhysicsQueryFilter& filter, PhysicsHit* hit)
{
    Vec3 d;
    if (shape.Validate() != Validation::Ok || shape.kind == ShapeKind::Mesh || shape.kind == ShapeKind::Compound ||
        !Finite(position) || !Unit(direction, &d) || !IsFinite(distance) || !(distance > 0.0f))
    {
        return false;
    }
    const Libraries libs = LibrariesOf(world);
    const Vec3 half = shape.LocalHalfExtents();
    const f32 smallest = 2.0f * Min(half.x, Min(half.y, half.z));
    const f32 spacing = Max(0.25f * smallest, 1e-3f);
    u32 samples = static_cast<u32>(distance / spacing) + 1;
    if (samples > 1024)
    {
        samples = 1024;
    }
    bool found = false;
    PhysicsHit best;
    best.distance = kBig;
    const Aabb start = shape.Bounds(position, rotation);
    const Aabb end = shape.Bounds(position + d * distance, rotation);
    Aabb swept;
    swept.min = Vec3(Min(start.min.x, end.min.x), Min(start.min.y, end.min.y), Min(start.min.z, end.min.z));
    swept.max = Vec3(Max(start.max.x, end.max.x), Max(start.max.y, end.max.y), Max(start.max.z, end.max.z));
    ForEachCollider(world, filter,
                    [&](ColliderId id, const ColliderRecord& record, const Body& body)
                    {
                        const Pose target = PoseOf(body, record.value);
                        const Aabb box = record.value.shape.Bounds(target.position, target.rotation);
                        if (box.min.x > swept.max.x || box.max.x < swept.min.x || box.min.y > swept.max.y ||
                            box.max.y < swept.min.y || box.min.z > swept.max.z || box.max.z < swept.min.z)
                        {
                            return;
                        }
                        const auto touches = [&](f32 t, ContactManifold* manifold)
                        {
                            Pose pose;
                            pose.position = position + d * (distance * t);
                            pose.rotation = rotation;
                            return Collide(shape, pose, record.value.shape, target, 0.0f, manifold, libs.hulls,
                                           libs.meshes, libs.compounds) == CollideResult::Contact;
                        };
                        ContactManifold manifold;
                        f32 toi = -1.0f;
                        if (touches(0.0f, &manifold))
                        {
                            toi = 0.0f;
                        }
                        else
                        {
                            for (u32 k = 1; k <= samples && toi < 0.0f; ++k)
                            {
                                const f32 t = static_cast<f32>(k) / static_cast<f32>(samples);
                                if (!touches(t, &manifold))
                                {
                                    continue;
                                }
                                f32 lo = static_cast<f32>(k - 1) / static_cast<f32>(samples);
                                f32 hi = t;
                                for (u32 b = 0; b < 30; ++b)
                                {
                                    const f32 mid = 0.5f * (lo + hi);
                                    ContactManifold scratch;
                                    if (touches(mid, &scratch))
                                    {
                                        hi = mid;
                                    }
                                    else
                                    {
                                        lo = mid;
                                    }
                                }
                                toi = hi;
                                touches(hi, &manifold);
                            }
                        }
                        if (toi < 0.0f)
                        {
                            return;
                        }
                        const f32 moved = toi * distance;
                        if (moved < best.distance || (moved == best.distance && id.ToKey() < best.collider.ToKey()))
                        {
                            best.collider = id;
                            best.body = record.owner;
                            best.distance = moved;
                            best.normal = manifold.count > 0 ? manifold.normal * -1.0f : d * -1.0f;
                            best.point = manifold.count > 0 ? manifold.points[0].position_b : position + d * moved;
                            best.triangle = kNoTriangle;
                            found = true;
                        }
                    });
    if (found && hit != nullptr)
    {
        *hit = best;
    }
    return found;
}

usize Overlap(const PhysicsWorld& world, const Shape& shape, const Vec3& position, const Quaternion& rotation,
              const PhysicsQueryFilter& filter, Vector<ColliderId>* out)
{
    out->Clear();
    if (shape.Validate() != Validation::Ok || !Finite(position))
    {
        return 0;
    }
    const Libraries libs = LibrariesOf(world);
    Pose pose;
    pose.position = position;
    pose.rotation = rotation;
    const Aabb mine = shape.Bounds(position, rotation);
    ForEachCollider(world, filter,
                    [&](ColliderId id, const ColliderRecord& record, const Body& body)
                    {
                        const Pose target = PoseOf(body, record.value);
                        const Aabb box = record.value.shape.Bounds(target.position, target.rotation);
                        if (box.min.x > mine.max.x || box.max.x < mine.min.x || box.min.y > mine.max.y ||
                            box.max.y < mine.min.y || box.min.z > mine.max.z || box.max.z < mine.min.z)
                        {
                            return;
                        }
                        ContactManifold manifold;
                        if (Collide(shape, pose, record.value.shape, target, 0.0f, &manifold, libs.hulls, libs.meshes,
                                    libs.compounds) == CollideResult::Contact)
                        {
                            out->Push(id);
                        }
                    });
    Sort(Span<ColliderId>(out->Data(), out->Size()),
         [](const ColliderId& a, const ColliderId& b) { return a.ToKey() < b.ToKey(); });
    return out->Size();
}

bool ClosestPoint(const PhysicsWorld& world, const Vec3& point, f32 max_distance, const PhysicsQueryFilter& filter,
                  PhysicsHit* hit)
{
    if (!Finite(point) || !IsFinite(max_distance) || !(max_distance > 0.0f))
    {
        return false;
    }
    const Libraries libs = LibrariesOf(world);
    constexpr f32 kProbe = 1e-4f;
    const Shape probe = Shape::MakeSphere(kProbe);
    Pose probe_pose;
    probe_pose.position = point;
    bool found = false;
    PhysicsHit best;
    best.distance = kBig;
    ForEachCollider(world, filter,
                    [&](ColliderId id, const ColliderRecord& record, const Body& body)
                    {
                        const Pose target = PoseOf(body, record.value);
                        ContactManifold manifold;
                        if (Collide(probe, probe_pose, record.value.shape, target, max_distance, &manifold, libs.hulls,
                                    libs.meshes, libs.compounds) != CollideResult::Contact ||
                            manifold.count == 0)
                        {
                            return;
                        }
                        // A negative depth is the gap between the shapes; the probe's own radius is part of the
                        // distance.
                        const f32 depth = manifold.points[0].depth;
                        const f32 distance = depth > 0.0f ? 0.0f : Max(0.0f, -depth + kProbe);
                        if (distance > max_distance)
                        {
                            return;
                        }
                        if (distance < best.distance ||
                            (distance == best.distance && id.ToKey() < best.collider.ToKey()))
                        {
                            best.collider = id;
                            best.body = record.owner;
                            best.distance = distance;
                            best.point = manifold.points[0].position_b;
                            best.normal = manifold.normal * -1.0f;
                            found = true;
                        }
                    });
    if (found && hit != nullptr)
    {
        *hit = best;
    }
    return found;
}

}  // namespace engine_placeholder::physics
