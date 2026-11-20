// Compound shapes in the narrow phase (Phase 25 Day 395): every child (or pair of children) is collided on its own and
// the contacts are merged into one manifold; a child's index is part of each point's feature id.
#include "containers/array.h"
#include "physics/collide.h"
#include "physics/collide_math.h"
#include "physics/compound.h"

namespace vse::physics::collide_math
{

namespace
{

// A child's pose in the world.
Pose ChildPose(const Pose& pose, const CompoundChild& child)
{
    Pose out;
    out.position = pose.position + pose.rotation.RotateVector(child.offset);
    out.rotation = pose.rotation * child.rotation;
    return out;
}

}  // namespace

bool CompoundPair(const Shape& a, const Pose& pose_a, const Shape& b, const Pose& pose_b, f32 margin,
                  const HullLibrary* hulls, const MeshLibrary* meshes, const CompoundLibrary* compounds,
                  ContactManifold* out)
{
    const CompoundShape* ca = a.kind == ShapeKind::Compound && compounds != nullptr ? compounds->Find(a.compound)
                                                                                    : nullptr;
    const CompoundShape* cb = b.kind == ShapeKind::Compound && compounds != nullptr ? compounds->Find(b.compound)
                                                                                    : nullptr;
    if ((a.kind == ShapeKind::Compound && ca == nullptr) || (b.kind == ShapeKind::Compound && cb == nullptr))
    {
        return false;
    }
    const u32 count_a = ca != nullptr ? ca->ChildCount() : 1u;
    const u32 count_b = cb != nullptr ? cb->ChildCount() : 1u;
    constexpr u32 kMaxFound = 64;
    Array<FoundContact, kMaxFound> found;
    u32 found_count = 0;
    for (u32 ia = 0; ia < count_a; ++ia)
    {
        const Shape& shape_a = ca != nullptr ? ca->Child(ia).shape : a;
        const Pose child_a = ca != nullptr ? ChildPose(pose_a, ca->Child(ia)) : pose_a;
        const Aabb box_a = shape_a.Bounds(child_a.position, child_a.rotation);
        for (u32 ib = 0; ib < count_b; ++ib)
        {
            const Shape& shape_b = cb != nullptr ? cb->Child(ib).shape : b;
            const Pose child_b = cb != nullptr ? ChildPose(pose_b, cb->Child(ib)) : pose_b;
            if (ca != nullptr && cb != nullptr &&
                !GroupsCollide(ca->Child(ia).group, ca->Child(ia).mask, cb->Child(ib).group, cb->Child(ib).mask))
            {
                continue;
            }
            Aabb box_b = shape_b.Bounds(child_b.position, child_b.rotation);
            box_b.min = box_b.min - Vec3(margin, margin, margin);
            box_b.max = box_b.max + Vec3(margin, margin, margin);
            if (!Overlaps(box_a, box_b))
            {
                continue;
            }
            ContactManifold m;
            if (physics::Collide(shape_a, child_a, shape_b, child_b, margin, &m, hulls, meshes, compounds) !=
                CollideResult::Contact)
            {
                continue;
            }
            for (u32 k = 0; k < m.count && found_count < kMaxFound; ++k)
            {
                FoundContact& f = found[found_count++];
                f.normal = m.normal;
                f.pa = m.points[k].position_a;
                f.pb = m.points[k].position_b;
                f.depth = m.points[k].depth;
                f.id = m.points[k].feature_id * 2654435761u ^ (ia + 1u) * 40503u ^ (ib + 1u) * 0x9E3779B1u;
            }
        }
    }
    return MergeContacts(found.Data(), found_count, out);
}

}  // namespace vse::physics::collide_math
