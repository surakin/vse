#pragma once

//! Transforms and the parent-child hierarchy: `LocalTransform`, `WorldTransform`, `Parent`, and the system that
//! propagates a parent's world matrix to its children by depth, in parallel, writing a matrix only when it changed.

#include "ecs/command_buffer.h"
#include "ecs/entity.h"
#include "ecs/schedule.h"
#include "ecs/world.h"
#include "math/matrix.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "types.h"

namespace vse::ecs
{

/// The deepest hierarchy: a root has depth 0, its children 1, and so on up to this value.
inline constexpr u32 kMaxHierarchyDepth = 64;

/// An entity's place relative to its parent (or to the world for a root): scale, then rotate, then translate.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct LocalTransform
{
    /// Translation, applied last.
    Vec3 position;
    /// Orientation, applied after scale.
    Quaternion rotation = Quaternion::Identity();
    /// Per-axis scale, applied first.
    Vec3 scale = Vec3(1.0f, 1.0f, 1.0f);

    /// The matrix that scales, then rotates, then translates (column-vector convention, as in `math/transform.h`).
    /// - Fails: never.
    Mat4 ToMatrix() const;
};

/// An entity's place in the world: its parents' matrices times its own. Written by the propagation system; read by
/// everything else.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct WorldTransform
{
    /// The local-to-world matrix.
    Mat4 matrix = Mat4::Identity();
};

/// Makes an entity a child of `entity`. Set it with [[ecs::SetParent]], not with a raw `Set<Parent>` (which the
/// propagation then has to repair).
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing; the parent is an id, destroying the parent does not destroy the child.
struct Parent
{
    /// The parent.
    Entity entity;
};

/// The number of parents above an entity (a child of a root has 1). Kept by [[ecs::SetParent]] and
/// [[ecs::ClearParent]]; a root has none.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct HierarchyDepth
{
    /// The depth.
    u32 depth = 0;
};

/// What the propagation did, as a world resource ([[ecs::World::SetResource]] it before running
/// [[ecs::MakeTransformSystem]]).
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: owns nothing.
struct TransformReport
{
    /// Frames the propagation ran.
    u64 frames = 0;
    /// Matrices computed (roots with a changed local transform, and every child in a frame where something changed).
    u64 recomputed = 0;
    /// Matrices written because they differed from the stored one.
    u64 written = 0;
    /// Children whose parent was destroyed: their `Parent` was removed, so they became roots.
    u64 dangling_fixed = 0;
    /// Children whose stored depth disagreed with their parent's and was recomputed.
    u64 depth_repaired = 0;
    /// Children in a cycle or deeper than the maximum: they kept their previous matrix.
    u64 unresolved = 0;
    /// Times the hierarchy's children index was made again because `Parent` changed outside `SetParent` and
    /// `ClearParent` (a raw `Set<Parent>`, a removal, a destruction, an orphan's repair); 1 for its first use.
    u64 index_rebuilds = 0;
    /// `SetParent` calls refused for exceeding the maximum depth.
    u64 refused_depth = 0;
};

/// What [[ecs::SetParent]] says.
enum class HierarchyStatus : u8
{
    /// Done.
    Ok,
    /// The child or the parent is not alive.
    DeadEntity,
    /// The child and the parent are the same entity.
    SelfParent,
    /// The parent is a descendant of the child.
    Cycle,
    /// The result would be deeper than [[ecs::kMaxHierarchyDepth]] (counted in the world's `TransformReport` when it
    /// has one).
    TooDeep,
    /// The world could not add the components (a limit was reached).
    Refused,
};

/// The local matrix as a decomposed transform (position, rotation, scale from the lengths of the basis vectors). Shear
/// is dropped.
///
/// - Param matrix: an affine matrix.
/// - Returns: the transform; a basis with a zero axis gives scale 0 on it and the identity rotation.
/// - Fails: never.
LocalTransform Decompose(const Mat4& matrix);
/// A transform at `eye` whose -Z axis points at `target`, with `up` as the approximate up direction (the camera
/// convention; use the opposite for a body that faces +Z).
///
/// - Returns: the transform with unit scale; the identity rotation when `eye == target` or `up` is parallel to the view
/// direction.
/// - Fails: never.
LocalTransform LookAt(const Vec3& eye, const Vec3& target, const Vec3& up);
/// Creates an entity with a [[ecs::LocalTransform]] and a [[ecs::WorldTransform]] (the world matrix is filled in by the
/// propagation).
///
/// - Returns: the entity, or the invalid entity when the world refused.
/// - Aborts: the world is being iterated.
Entity CreateTransformEntity(World& world, const LocalTransform& local);

/// Makes `parent` the parent of `child`, keeping the depths of the child and of all its descendants right.
///
/// The cost is the child's subtree (a children index kept beside the hierarchy, query and hierarchy indices series Day
/// 2), plus one pass over every entity that has a `Parent` when `Parent` changed outside these calls since the last one
/// (a raw `Set<Parent>`, a removal, a destruction, the propagation's repair of an orphan; see
/// [[ecs::World::ComponentVersion]]).
///
/// - Param parent: the new parent; it need not have transforms.
/// - Returns: the status; nothing changes unless it is `Ok`.
/// - Aborts: the world is being iterated.
HierarchyStatus SetParent(World& world, Entity child, Entity parent);
/// Makes `child` a root again (removes `Parent` and `HierarchyDepth`) and fixes its descendants' depths. Its world
/// matrix is recomputed from its local one.
///
/// - Returns: `Ok`, also when it had no parent; `DeadEntity` for a dead child.
/// - Aborts: the world is being iterated.
HierarchyStatus ClearParent(World& world, Entity child);
/// Records the destruction of `root` and of every entity below it into `commands` (a parent's destruction alone leaves
/// its children as roots).
///
/// - Param commands: the buffer to record into; apply it at a sync point.
/// - Returns: the number of destroy commands recorded.
/// - Aborts: the world is being iterated by a query that writes `Parent` (it reads `Parent`).
u32 DestroyRecursive(World& world, CommandBuffer& commands, Entity root);

/// The propagation as a system: reads `LocalTransform`, `Parent` and `HierarchyDepth`, writes `WorldTransform`, and
/// writes the [[ecs::TransformReport]] resource.
///
/// Roots whose local transform changed get `local.ToMatrix()`. When anything changed, children are processed depth by
/// depth (each depth in parallel when the system has the frame thread to itself), world = parent world * local, and a
/// matrix is written, and its chunk stamped as written, only if it differs from the stored one; a frame where nothing
/// changed costs a few `Changed` checks. Policies: a child whose parent was destroyed loses `Parent` and becomes a
/// root; a child in a cycle, or deeper than 64, keeps its previous matrix (counted); a stored depth that disagrees with
/// the parent's (a raw `Set<Parent>`) is recomputed from the chain, the child is processed at the right depth at once,
/// and its own descendants catch up the next frame.
///
/// - Param name: a string literal naming the system.
/// - Returns: the system; give it to [[ecs::Schedule::Add]] (add `InStage` or `After` as needed).
SystemDesc MakeTransformSystem(const char* name);

}  // namespace vse::ecs
