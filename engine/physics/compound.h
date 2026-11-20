#pragma once

//! Compound shapes (Phase 25 Day 395): several shapes on one body, each with a local transform, a material and its own
//! group and mask. The mass, the centre of mass and the inertia of the whole are computed once when the compound is
//! made.

#include "containers/shared_ptr.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "physics/components.h"
#include "physics/hull.h"
#include "physics/mass.h"
#include "physics/shapes.h"
#include "physics/validation.h"
#include "types.h"

namespace vse::physics
{

/// The most children a compound holds.
inline constexpr u32 kMaxCompoundChildren = 64;

/// One shape of a compound, in the compound's space.
///
/// - Thread-safety: thread-compatible plain data.
struct CompoundChild
{
    /// A sphere, box, capsule or hull (not a mesh or another compound).
    Shape shape;
    /// Where the child's origin is, in the compound's space.
    Vec3 offset = Vec3(0.0f, 0.0f, 0.0f);
    /// The child's orientation in the compound's space (a unit quaternion).
    Quaternion rotation = Quaternion::Identity();
    /// The child's surface and density (the compound's mass is the sum of the children's).
    PhysicsMaterial material;
    /// The groups the child is in, one bit each.
    u32 group = 1;
    /// The groups the child collides with, one bit each (only between two compounds' children).
    u32 mask = 0xFFFFFFFFu;
};

class CompoundShape;
/// The shared compound, mutable form (used while one is built).
using CompoundShapePtr = SharedPtr<CompoundShape>;
/// The shared compound, read-only: the way to hold one.
using CompoundShapeConstPtr = SharedPtr<const CompoundShape>;

/// An immutable compound: the children and the mass data computed from them.
///
/// - Thread-safety: thread-safe after `Create` (immutable).
/// - Ownership: shared by every collider that uses it, through [[CompoundShapeConstPtr]].
class CompoundShape
{
public:
    CompoundShape() = default;
    CompoundShape(const CompoundShape&) = delete;
    CompoundShape& operator=(const CompoundShape&) = delete;

    /// Checks the children and computes the whole's mass, centre of mass and inertia.
    ///
    /// - Param children: one to 64 children.
    /// - Param hulls: the library the hull children's ids refer to; may be null when none is a hull.
    /// - Param result: receives the reason on failure; may be null.
    /// - Returns: the compound, or an empty pointer.
    /// - Ownership: copies the children; nothing is borrowed after the call.
    /// - Fails: `OutOfRange` for no children, more than 64, a mesh or compound child or an unknown hull; the
    /// validation of the child's shape, offset, rotation or material otherwise.
    static CompoundShapeConstPtr Create(Span<const CompoundChild> children, const HullLibrary* hulls,
                                        Validation* result);

    /// The number of children.
    /// - Fails: never.
    u32 ChildCount() const { return static_cast<u32>(children_.Size()); }
    /// Child `i`.
    ///
    /// - Aborts: `i` out of range.
    const CompoundChild& Child(u32 i) const;
    /// The mass properties of the whole, in the compound's space: `center` is the centre of mass, the inertia is
    /// about it along the principal axes `principal_rotation` gives.
    /// - Fails: never.
    const MassProperties& Mass() const { return mass_; }
    /// The bound of the compound about its origin (what `Shape::MakeCompound` takes), at least 1e-3.
    /// - Fails: never.
    const Vec3& Extent() const { return extent_; }

private:
    Vector<CompoundChild> children_;
    MassProperties mass_;
    Vec3 extent_ = Vec3(0.0f, 0.0f, 0.0f);
};

/// The compounds a world's colliders refer to by id (1, 2, ...).
///
/// - Thread-safety: thread-compatible: reads are safe while nothing adds.
class CompoundLibrary
{
public:
    /// Adds a compound.
    ///
    /// - Returns: its id (1, 2, ...), never 0; 0 for a null compound.
    /// - Fails: a null compound returns 0.
    u32 Add(const CompoundShapeConstPtr& compound);
    /// The number of compounds.
    /// - Fails: never.
    u32 Count() const { return static_cast<u32>(compounds_.Size()); }
    /// Finds a compound.
    ///
    /// - Returns: the compound, or null for an unknown id.
    /// - Ownership: owned by the library.
    /// - Fails: an unknown id returns null.
    const CompoundShape* Find(u32 id) const;

private:
    Vector<CompoundShapeConstPtr> compounds_;
};

/// Whether two colliders' group and mask words let them collide: each one's group meets the other's mask.
///
/// - Param group_a: the first's groups.
/// - Param mask_a: the first's mask.
/// - Param group_b: the second's groups.
/// - Param mask_b: the second's mask.
/// - Returns: true when `(group_a & mask_b) != 0 && (group_b & mask_a) != 0`.
/// - Fails: never.
inline bool GroupsCollide(u32 group_a, u32 mask_a, u32 group_b, u32 mask_b)
{
    return (group_a & mask_b) != 0 && (group_b & mask_a) != 0;
}

}  // namespace vse::physics
