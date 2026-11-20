// The narrow phase's dispatch and the tests of the primitive shapes (Phase 25 Days 391-392): sphere-sphere,
// sphere-capsule, capsule-capsule, sphere-box, box-box and capsule-box.
#pragma once

#include "physics/compound.h"
#include "physics/contact.h"
#include "physics/hull.h"
#include "physics/shapes.h"
#include "physics/triangle_mesh.h"
#include "types.h"

namespace vse::physics
{

/// Finds the contact between two shapes, the one entry point of the narrow phase.
///
/// The table, by the kinds of A and B (either order; the result is the same as for the other order, flipped):
/// every pair of sphere, capsule and box is built by a closed form or the separating axis (Days 391-392) and every pair
/// with a convex hull by GJK and EPA (Day 393). A pair without a test returns `Unsupported` and leaves the manifold
/// alone. Only `+ - * /` and `Sqrt` are used, so the result is bit-identical on every machine.
///
/// - Param a: shape A.
/// - Param pose_a: where A is.
/// - Param b: shape B.
/// - Param pose_b: where B is.
/// - Param margin: the contact margin, at least 0: shapes apart by less than this still make a contact, with a negative
/// depth.
/// - Param manifold: receives the contact when the result is `Contact`; not null. Its friction and restitution are not
/// touched.
/// - Param hulls: the library the hull shapes' ids refer to; may be null when neither shape is a hull (a hull that is
/// not in it makes the result `None`).
/// - Returns: `Contact`, `None` or `Unsupported`.
/// - Param meshes: the library the mesh shapes' ids refer to; may be null when neither shape is a mesh (a mesh that is
/// not in it gives `None`). A mesh against a mesh is `Unsupported`.
/// - Param compounds: the library the compound shapes' ids refer to; may be null when neither shape is a compound (a
/// compound that is not in it gives `None`). A compound is collided child by child and the contacts merge (their
/// feature ids carry the child indices).
/// - Ownership: `manifold` is the caller's and the libraries are borrowed; nothing is kept.
/// - Fails: an invalid shape returns `None`.
CollideResult Collide(const Shape& a, const Pose& pose_a, const Shape& b, const Pose& pose_b, f32 margin,
                      ContactManifold* manifold, const HullLibrary* hulls = nullptr,
                      const MeshLibrary* meshes = nullptr, const CompoundLibrary* compounds = nullptr);

}  // namespace vse::physics
