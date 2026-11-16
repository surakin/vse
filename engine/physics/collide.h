// The narrow phase's dispatch and the tests with closed forms (Phase 25 Day 379): sphere-sphere, sphere-capsule,
// capsule-capsule and sphere-box.
#pragma once

#include "physics/contact.h"
#include "physics/shapes.h"
#include "types.h"

namespace engine_placeholder::physics
{

/// Finds the contact between two shapes, the one entry point of the narrow phase.
///
/// The table, by the kinds of A and B (either order; the result is the same as for the other order, flipped):
/// sphere-sphere, sphere-capsule, capsule-capsule and sphere-box are built (Day 379); box-box and capsule-box are Day
/// 380; hulls Day 381. A pair without a test returns `Unsupported` and leaves the manifold alone. Only `+ - * /` and
/// `Sqrt` are used, so the result is bit-identical on every machine.
///
/// - Param a: shape A.
/// - Param pose_a: where A is.
/// - Param b: shape B.
/// - Param pose_b: where B is.
/// - Param margin: the contact margin, at least 0: shapes apart by less than this still make a contact, with a negative
/// depth.
/// - Param manifold: receives the contact when the result is `Contact`; not null. Its friction and restitution are not
/// touched.
/// - Returns: `Contact`, `None` or `Unsupported`.
/// - Ownership: `manifold` is the caller's; nothing is kept.
/// - Fails: an invalid shape returns `None`.
CollideResult Collide(const Shape& a, const Pose& pose_a, const Shape& b, const Pose& pose_b, f32 margin,
                      ContactManifold* manifold);

}  // namespace engine_placeholder::physics
