// The collision shapes of the physics module (Phase 25 Day 387): sphere, box and capsule as plain values with their
// bounds. Convex hulls joined on Day 393; triangle meshes and compound shapes join on Days 394-395, each as a new kind
// at the end of `ShapeKind`.
#pragma once

#include "math/geometry.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "physics/validation.h"
#include "types.h"

namespace vse::physics
{

/// The kind of a shape. The numbers are stored and sent through the C API: never renumbered, new kinds go at the end.
///
/// - Thread-safety: thread-compatible plain data.
enum class ShapeKind : u8
{
    Sphere = 0,    ///< A ball: `radius`.
    Box = 1,       ///< A box with its centre at the shape's origin: `half_extents`.
    Capsule = 2,   ///< A cylinder with a hemisphere on each end, its axis along the shape's Y axis: `half_height` (of
                   ///< the cylinder part) and `radius`.
    Hull = 3,      ///< A convex hull from the world's `HullLibrary`: `hull` (its id) and `half_extents` (its bound).
    Mesh = 4,      ///< A triangle mesh from the world's `MeshLibrary`: `mesh` (its id) and `half_extents` (its bound).
                   ///< Static bodies only.
    Compound = 5,  ///< Several shapes with local transforms from the world's `CompoundLibrary`: `compound` (its id) and
                   ///< `half_extents` (its bound).
    HeightField = 6,  ///< A grid of heights from the world's `HeightFieldLibrary`: `height_field` (its id) and
                      ///< `half_extents` (its bound). Static bodies only.
};

/// A shape as a tagged value: the kind says which fields count. The shape lives in the space of its collider (its
/// origin is the collider's offset).
///
/// - Thread-safety: thread-compatible plain data.
struct Shape
{
    /// Which fields are used.
    ShapeKind kind = ShapeKind::Sphere;
    /// For a sphere and a capsule, the radius, above zero.
    f32 radius = 0.5f;
    /// For a capsule, half the length of the cylinder part (zero makes it a sphere), at least zero.
    f32 half_height = 0.0f;
    /// For a box, the half extents on x, y and z, each above zero.
    Vec3 half_extents = Vec3(0.5f, 0.5f, 0.5f);
    /// For a hull, its id in the world's `HullLibrary`, at least 1.
    u32 hull = 0;
    /// For a mesh, its id in the world's `MeshLibrary`, at least 1.
    u32 mesh = 0;
    /// For a compound, its id in the world's `CompoundLibrary`, at least 1.
    u32 compound = 0;
    /// For a height field, its id in the world's `HeightFieldLibrary`, at least 1.
    u32 height_field = 0;

    /// Makes a sphere.
    ///
    /// - Param radius: its radius.
    /// - Returns: the shape (not checked: see `Validate`).
    /// - Fails: never.
    static Shape MakeSphere(f32 radius);

    /// Makes a box.
    ///
    /// - Param half_extents: half its size on each axis.
    /// - Returns: the shape (not checked: see `Validate`).
    /// - Fails: never.
    static Shape MakeBox(const Vec3& half_extents);

    /// Makes a capsule along Y.
    ///
    /// - Param half_height: half the length of the cylinder part.
    /// - Param radius: the radius of the cylinder and of the end caps.
    /// - Returns: the shape (not checked: see `Validate`).
    /// - Fails: never.
    static Shape MakeCapsule(f32 half_height, f32 radius);

    /// Makes a convex hull shape.
    ///
    /// - Param hull: the hull's id in the `HullLibrary` (`HullLibrary::Add` returned it).
    /// - Param extent: the hull's `ConvexHull::Extent()`: its bound about the shape's origin.
    /// - Returns: the shape (not checked: see `Validate`; the library is not consulted here).
    /// - Fails: never.
    static Shape MakeHull(u32 hull, const Vec3& extent);

    /// Makes a triangle mesh shape (static bodies only).
    ///
    /// - Param mesh: the mesh's id in the `MeshLibrary` (`MeshLibrary::Add` returned it).
    /// - Param extent: the mesh's `TriangleMesh::Extent()`: its bound about the shape's origin.
    /// - Returns: the shape (not checked: see `Validate`; the library is not consulted here).
    /// - Fails: never.
    static Shape MakeMesh(u32 mesh, const Vec3& extent);

    /// Makes a height field shape (static bodies only).
    ///
    /// - Param field: the field's id in the `HeightFieldLibrary` (`HeightFieldLibrary::Add` returned it).
    /// - Param extent: the field's `HeightField::Extent()`: its bound about the shape's origin.
    /// - Returns: the shape (not checked: see `Validate`; the library is not consulted here).
    /// - Fails: never.
    static Shape MakeHeightField(u32 field, const Vec3& extent);

    /// Makes a compound shape.
    ///
    /// - Param compound: the compound's id in the `CompoundLibrary` (`CompoundLibrary::Add` returned it).
    /// - Param extent: the compound's `CompoundShape::Extent()`: its bound about the shape's origin.
    /// - Returns: the shape (not checked: see `Validate`; the library is not consulted here).
    /// - Fails: never.
    static Shape MakeCompound(u32 compound, const Vec3& extent);

    /// Checks the shape: finite numbers, positive sizes, a known kind. Host data is refused with a result, never
    /// aborted on.
    ///
    /// - Returns: `Ok`, or why the shape was refused.
    /// - Fails: as the return says.
    Validation Validate() const;

    /// Returns the smallest axis-aligned box around the shape after it is rotated and moved.
    ///
    /// - Param position: where the shape's origin is.
    /// - Param rotation: its orientation (a unit quaternion).
    /// - Returns: the box; for a shape that is not valid, an empty box at `position`.
    /// - Fails: never.
    Aabb Bounds(const Vec3& position, const Quaternion& rotation) const;

    /// Returns the same box for a double-precision position (the extent is added to the position in `f64`, so the box
    /// is exact far from the origin).
    ///
    /// - Param position: where the shape's origin is, in the world.
    /// - Param rotation: its orientation (a unit quaternion).
    /// - Returns: the box; for a shape that is not valid, an empty box at `position`.
    /// - Fails: never.
    Aabbd Bounds(const Vec3d& position, const Quaternion& rotation) const;

    /// Returns how far the shape's box reaches from its origin on each axis once it is rotated (zero for a shape that
    /// is not valid).
    ///
    /// - Param rotation: its orientation (a unit quaternion).
    /// - Fails: never.
    Vec3 BoundsExtent(const Quaternion& rotation) const;

    /// Returns the half extents of the box around the shape in its own space (the capsule's are `radius, half_height +
    /// radius, radius`).
    ///
    /// - Returns: the half extents.
    /// - Fails: never.
    Vec3 LocalHalfExtents() const;
};

}  // namespace vse::physics
