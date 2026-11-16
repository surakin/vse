// The contact manifold and the pose of a shape (Phase 25 Day 379): what every narrow phase test produces and the solver
// of Day 384 consumes.
#pragma once

#include "containers/array.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "physics/components.h"
#include "types.h"

namespace engine_placeholder::physics
{

/// Where a shape is: the position of its origin and its orientation, in the world.
///
/// - Thread-safety: thread-compatible plain data.
struct Pose
{
    /// The origin of the shape.
    Vec3 position = Vec3(0.0f, 0.0f, 0.0f);
    /// Its orientation (a unit quaternion).
    Quaternion rotation = Quaternion::Identity();
};

/// One point of contact.
///
/// The depth is how far the shapes overlap along the normal: positive when they penetrate, negative when they are apart
/// by less than the contact margin (a speculative contact: the solver lets the shapes approach until the gap closes).
/// `position_a - position_b` along the normal is the depth.
///
/// - Thread-safety: thread-compatible plain data.
struct ContactPoint
{
    /// The point on the surface of shape A, in the world.
    Vec3 position_a = Vec3(0.0f, 0.0f, 0.0f);
    /// The point on the surface of shape B, in the world.
    Vec3 position_b = Vec3(0.0f, 0.0f, 0.0f);
    /// Penetration (positive) or separation (negative), in metres.
    f32 depth = 0.0f;
    /// Names the feature pair that made the point.
    ///
    /// It is the same from step to step while the same features (a face, an edge, a vertex of each shape) touch, so a
    /// contact cache can carry the solver's impulse over (warm starting).
    u32 feature_id = 0;
};

/// The contact between two shapes: up to four points sharing one normal.
///
/// - Thread-safety: thread-compatible plain data.
struct ContactManifold
{
    /// The unit normal, pointing from shape A toward shape B.
    Vec3 normal = Vec3(0.0f, 1.0f, 0.0f);
    /// The number of points used, 0 to 4.
    u32 count = 0;
    /// The points; the first `count` count.
    Array<ContactPoint, 4> points;
    /// The combined friction of the two colliders (`CombineMaterials`).
    f32 friction = 0.0f;
    /// The combined restitution.
    f32 restitution = 0.0f;

    /// Turns the manifold around: the normal is negated and the two points of every contact swap, as if the shapes were
    /// named in the other order.
    ///
    /// - Fails: never.
    void Flip()
    {
        normal = Vec3(-normal.x, -normal.y, -normal.z);
        for (u32 i = 0; i < count; ++i)
        {
            const Vec3 a = points[i].position_a;
            points[i].position_a = points[i].position_b;
            points[i].position_b = a;
        }
    }
};

/// What a collision test found.
///
/// - Thread-safety: thread-compatible plain data.
enum class CollideResult : u8
{
    None = 0,     ///< The shapes are apart by more than the margin: no manifold.
    Contact = 1,  ///< The manifold holds at least one point.
    Unsupported,  ///< No test exists for this pair of shapes yet (the day that adds it is named in
                  ///< `docs/guide/physics.md`).
};

/// Sets the friction and restitution of a manifold from the two colliders' materials.
///
/// Each value is made by `Combine` with the rule `CombineRuleFor` picks, so the order of the colliders does not matter.
///
/// - Param a: the material of collider A.
/// - Param b: the material of collider B.
/// - Param manifold: receives the friction and the restitution; not null.
/// - Ownership: `manifold` is the caller's; nothing is kept.
/// - Fails: never.
void CombineMaterials(const PhysicsMaterial& a, const PhysicsMaterial& b, ContactManifold* manifold);

}  // namespace engine_placeholder::physics
