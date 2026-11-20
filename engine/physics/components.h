// The physics components of an entity (Phase 25 Day 387): the body, its velocity and its collider. They are plain data:
// the world that steps them is Day 388.
#pragma once

#include "math/quaternion.h"
#include "math/vector.h"
#include "physics/shapes.h"
#include "physics/validation.h"
#include "types.h"

namespace vse::physics
{

/// How a body takes part in the simulation. The numbers are stored and sent through the C API: never renumbered.
///
/// - Thread-safety: thread-compatible plain data.
enum class BodyType : u8
{
    Static = 0,     ///< Never moves; has no mass; collides with dynamic bodies.
    Kinematic = 1,  ///< Moved by the program (its velocity), not by forces; pushes dynamic bodies, is not pushed.
    Dynamic = 2,    ///< Moved by forces, gravity and contacts.
};

/// Flags of a body, ORed in `RigidBody::flags`.
///
/// - Thread-safety: thread-compatible plain data.
enum BodyFlags : u32
{
    kBodyLockRotation = 1u << 0,       ///< The body does not rotate (its inverse inertia is zero).
    kBodyNoSleep = 1u << 1,            ///< The body never falls asleep (the default is that it may).
    kBodyContinuous = 1u << 2,         ///< Continuous collision detection is used for this body (Day 402).
    kBodyKnownFlags = (1u << 3) - 1u,  ///< Every flag this build knows, ORed (a flag outside it is refused).
};

/// How the friction or the restitution of two touching colliders is made one number.
///
/// - Thread-safety: thread-compatible plain data.
enum class CombineRule : u8
{
    Average = 0,   ///< The mean of the two.
    Minimum = 1,   ///< The smaller.
    Multiply = 2,  ///< The product.
    Maximum = 3,   ///< The larger.
};

/// The surface of a collider.
///
/// - Thread-safety: thread-compatible plain data.
struct PhysicsMaterial
{
    /// The Coulomb friction coefficient, at least zero.
    f32 friction = 0.5f;
    /// The bounce: 0 stops, 1 keeps all the speed; from 0 to 1.
    f32 restitution = 0.0f;
    /// Kilograms per cubic metre, above zero; the mass of a body is its colliders' density times their volume.
    f32 density = 1000.0f;
    /// How the two frictions become one.
    CombineRule friction_rule = CombineRule::Average;
    /// How the two restitutions become one.
    CombineRule restitution_rule = CombineRule::Maximum;

    /// Checks the numbers: finite, friction at least zero, restitution within 0 and 1, density above zero, known rules.
    ///
    /// - Returns: `Ok`, or why it was refused.
    /// - Fails: as the return says.
    Validation Validate() const;
};

/// Combines two values by a rule. Which rule applies to a pair is `CombineRuleFor`'s choice, so the result does not
/// depend on which collider is first.
///
/// - Param rule: how.
/// - Param a: the first value.
/// - Param b: the second value.
/// - Returns: the combined value.
/// - Fails: never.
f32 Combine(CombineRule rule, f32 a, f32 b);

/// Picks the rule used for a pair of colliders: the one with the larger number.
///
/// - Param a: the first collider's rule.
/// - Param b: the second collider's rule.
/// - Returns: the rule.
/// - Fails: never.
CombineRule CombineRuleFor(CombineRule a, CombineRule b);

/// The body of an entity: how it moves and how heavy it is.
///
/// - Thread-safety: thread-compatible plain data.
struct RigidBody
{
    /// Static, kinematic or dynamic.
    BodyType type = BodyType::Dynamic;
    /// `BodyFlags`.
    u32 flags = 0;
    /// The mass in kilograms; zero means "from the colliders' density" (Day 388). At least zero.
    f32 mass = 0.0f;
    /// The fraction of the linear speed lost each second (`v *= 1 / (1 + c dt)`), at least zero.
    f32 linear_damping = 0.0f;
    /// The same for the angular speed.
    f32 angular_damping = 0.05f;
    /// Multiplies gravity for this body (zero floats it, negative lifts it).
    f32 gravity_scale = 1.0f;

    /// Checks the numbers: a known type and flags, finite, mass and damping at least zero.
    ///
    /// - Returns: `Ok`, or why it was refused.
    /// - Fails: as the return says.
    Validation Validate() const;
};

/// The speed of an entity's body, in metres and radians per second in the world.
///
/// - Thread-safety: thread-compatible plain data.
struct Velocity
{
    /// The linear velocity.
    Vec3 linear = Vec3(0.0f, 0.0f, 0.0f);
    /// The angular velocity: the axis is the axis of rotation, the length the radians per second.
    Vec3 angular = Vec3(0.0f, 0.0f, 0.0f);

    /// Checks that every number is finite.
    ///
    /// - Returns: `Ok` or `NotFinite`.
    /// - Fails: as the return says.
    Validation Validate() const;
};

/// The shape of an entity's body and where it sits on it.
///
/// - Thread-safety: thread-compatible plain data.
struct Collider
{
    /// The shape, in the collider's space.
    Shape shape;
    /// Where the collider's origin is, in the body's space.
    Vec3 offset = Vec3(0.0f, 0.0f, 0.0f);
    /// The collider's orientation in the body's space (a unit quaternion).
    Quaternion rotation = Quaternion::Identity();
    /// The surface.
    PhysicsMaterial material;
    /// The collision layer this collider is on (0 to 31).
    u8 layer = 0;
    /// A trigger reports overlaps and does not collide (Day 395).
    bool is_trigger = false;
    /// The groups the collider is in, one bit each (Day 395).
    u32 group = 1;
    /// The groups it collides with, one bit each: a pair collides when each one's group meets the other's mask.
    u32 mask = 0xFFFFFFFFu;

    /// Checks the shape, the material, the offset, the rotation (a unit quaternion within 1e-3) and the layer.
    ///
    /// - Returns: `Ok`, or why it was refused.
    /// - Fails: as the return says.
    Validation Validate() const;
};

}  // namespace vse::physics
