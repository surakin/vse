// The physics world (Phase 25 Day 376): the bodies and colliders of a simulation, with stable ids. The steps that move
// them come on Day 378.
#pragma once

#include "ecs/entity.h"
#include "math/matrix.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "physics/components.h"
#include "physics/ids.h"
#include "physics/mass.h"
#include "physics/slot_pool.h"
#include "types.h"

namespace engine_placeholder::physics
{

/// What a call on the world did.
///
/// - Thread-safety: thread-compatible plain data.
enum class BodyStatus : u8
{
    Ok = 0,       ///< Done.
    StaleId,      ///< The id is null, was destroyed or never existed.
    Invalid,      ///< A number is not finite, or a component was refused by its `Validate()`.
    WrongType,    ///< The call needs another body type (a force on a static body).
    Unsupported,  ///< Not built yet (a second collider on a body: Day 383).
    OutOfMemory,  ///< An allocation failed.
};

/// Returns the name of a status, static and never empty.
///
/// - Param status: the status.
/// - Returns: its name.
/// - Thread-safety: thread-safe.
/// - Ownership: the string is static; the caller does not free it.
/// - Fails: never.
const char* BodyStatusName(BodyStatus status);

/// What a new body is made from.
///
/// - Thread-safety: thread-compatible plain data.
struct BodyDesc
{
    /// The body's type, mass override, damping and flags.
    RigidBody settings;
    /// Where the body's origin is.
    Vec3 position = Vec3(0.0f, 0.0f, 0.0f);
    /// How it is turned (a unit quaternion).
    Quaternion orientation = Quaternion::Identity();
    /// Its speed.
    Velocity velocity;
    /// The ECS entity the body belongs to (invalid for a body made by hand).
    ecs::Entity entity;
};

/// One body: its state and mass. The world's accessors are the way to change it; the step (Day 378) reads and writes
/// the fields.
///
/// - Thread-safety: thread-compatible plain data.
struct Body
{
    /// The id this body was made with.
    BodyId id;
    /// The ECS entity it belongs to, or an invalid one.
    ecs::Entity entity;
    /// Type, mass override, damping, flags.
    RigidBody settings;
    /// The origin of the body in the world.
    Vec3 position = Vec3(0.0f, 0.0f, 0.0f);
    /// The body's orientation.
    Quaternion orientation = Quaternion::Identity();
    /// The linear velocity of the centre of mass.
    Vec3 linear_velocity = Vec3(0.0f, 0.0f, 0.0f);
    /// The angular velocity.
    Vec3 angular_velocity = Vec3(0.0f, 0.0f, 0.0f);
    /// The force applied since the last step, in newtons (cleared by the step).
    Vec3 force = Vec3(0.0f, 0.0f, 0.0f);
    /// The torque applied since the last step.
    Vec3 torque = Vec3(0.0f, 0.0f, 0.0f);
    /// The mass, centre of mass and inertia (zero mass for a static or kinematic body).
    MassProperties mass;
    /// The inverse of the mass, zero for a body that is not dynamic.
    f32 inverse_mass = 0.0f;
    /// The inverse inertia tensor in the world, refreshed whenever the orientation changes.
    Mat3 inverse_inertia_world = Mat3::Identity();
    /// The first collider of the body, or null.
    ColliderId first_collider;
    /// How many colliders the body has.
    u32 collider_count = 0;
    /// Whether the body is asleep (always false until Day 388).
    bool sleeping = false;
    /// Seconds below the sleep thresholds (Day 388).
    f32 sleep_time = 0.0f;
    /// The island the body is in (Day 388).
    u32 island = 0;
    /// Counts teleports; a contact cache remembers it to know its points are stale.
    u32 teleports = 0;
};

/// A collider of a body.
///
/// - Thread-safety: thread-compatible plain data.
struct ColliderRecord
{
    /// The collider's data.
    Collider value;
    /// The body it belongs to.
    BodyId owner;
    /// The next collider of the same body, or null.
    ColliderId next;
};

/// What the world holds.
///
/// - Thread-safety: thread-compatible plain data.
struct PhysicsWorldStats
{
    /// Bodies alive.
    u32 bodies = 0;
    /// Body slots ever made (does not grow when freed slots are reused).
    u32 body_slots = 0;
    /// Colliders alive.
    u32 colliders = 0;
    /// Collider slots ever made.
    u32 collider_slots = 0;
};

/// The bodies and colliders of a simulation. A pointer to a body or collider stays valid until it is destroyed (they
/// live in chunks that never move).
///
/// Ids are handed out in the order of the calls, reusing the most recently freed slot first, so two worlds built by the
/// same calls have the same ids; the ECS sync (`SyncBodies`) makes its calls in entity order for that reason. The world
/// is an ordinary value: keep it as an ECS resource
/// (`ecs::World::SetResource`) or by itself.
///
/// - Thread-safety: thread-compatible; not safe to change from a job while the step runs.
/// - Ownership: owns its bodies and colliders.
class PhysicsWorld
{
public:
    /// Makes a body, without a collider.
    ///
    /// - Param desc: where it is, how it moves.
    /// - Param status: receives why it failed (may be null).
    /// - Returns: the id, or the null id if `desc` is refused (not valid, a quaternion that is not a unit) or memory
    /// ran out.
    /// - Ownership: the world owns it; the pointer is valid until the body or collider is destroyed.
    /// - Fails: the null id, with `status`.
    BodyId CreateBody(const BodyDesc& desc, BodyStatus* status = nullptr);

    /// Destroys a body and its colliders. The id is stale afterwards.
    ///
    /// - Param id: the body.
    /// - Returns: `StaleId` if there is no such body.
    /// - Fails: as the return says.
    BodyStatus DestroyBody(BodyId id);

    /// Gives a body a collider and recomputes its mass.
    ///
    /// A body takes one collider for now (compound bodies: Day 383).
    ///
    /// - Param body: the body.
    /// - Param collider: the collider, checked with `Validate()`.
    /// - Param out: receives the collider's id (may be null).
    /// - Returns: `Ok`, `StaleId`, `Invalid`, `Unsupported` (a second collider) or `OutOfMemory`.
    /// - Ownership: the world owns it; the pointer is valid until the body or collider is destroyed.
    /// - Fails: as the return says.
    BodyStatus AddCollider(BodyId body, const Collider& collider, ColliderId* out = nullptr);

    /// Replaces the body's collider with another (or adds it when the body has none) and recomputes the mass.
    ///
    /// - Param body: the body.
    /// - Param collider: the new collider, checked with `Validate()`.
    /// - Returns: `Ok`, `StaleId`, `Invalid` or `OutOfMemory`.
    /// - Fails: as the return says.
    BodyStatus SetCollider(BodyId body, const Collider& collider);

    /// Replaces a body's settings and recomputes its mass.
    ///
    /// Changing the type of a body is supported at any time; a body made static loses its velocity.
    ///
    /// - Param id: the body.
    /// - Param settings: the new settings, checked with `Validate()`.
    /// - Returns: `Ok`, `StaleId` or `Invalid`.
    /// - Fails: as the return says.
    BodyStatus SetSettings(BodyId id, const RigidBody& settings);

    /// Looks a body up.
    ///
    /// - Param id: the id.
    /// - Returns: the body, or null for a stale id. The pointer stays valid until the body is destroyed.
    /// - Ownership: the world owns it; the pointer is valid until the body or collider is destroyed.
    /// - Fails: null for a stale id.
    Body* Find(BodyId id);

    /// Looks a body up.
    ///
    /// - Param id: the id.
    /// - Returns: the body, or null for a stale id.
    /// - Ownership: the world owns it; the pointer is valid until the body or collider is destroyed.
    /// - Fails: null for a stale id.
    const Body* Find(BodyId id) const;

    /// Looks a collider up.
    ///
    /// - Param id: the id.
    /// - Returns: the record, or null for a stale id.
    /// - Ownership: the world owns it; the pointer is valid until the body or collider is destroyed.
    /// - Fails: null for a stale id.
    const ColliderRecord* FindCollider(ColliderId id) const;

    /// Looks a collider up.
    ///
    /// - Param id: the id.
    /// - Returns: the record, or null for a stale id.
    /// - Ownership: the world owns it; the pointer is valid until the body or collider is destroyed.
    /// - Fails: null for a stale id.
    ColliderRecord* FindCollider(ColliderId id);

    /// Returns the id of the body in slot `index` if one is alive there (to walk every body in slot order).
    ///
    /// - Param index: the slot, below `Stats().body_slots`.
    /// - Returns: the id, or the null id for a free or unknown slot.
    /// - Fails: the null id.
    BodyId BodyAtSlot(u32 index) const;

    /// Returns the id of the collider in slot `index` if one is alive there.
    ///
    /// - Param index: the slot, below `Stats().collider_slots`.
    /// - Returns: the id, or the null id for a free or unknown slot.
    /// - Fails: the null id.
    ColliderId ColliderAtSlot(u32 index) const;

    /// Returns the world position of a body's centre of mass.
    ///
    /// - Param id: the body.
    /// - Returns: the point; the origin for a stale id.
    /// - Fails: never.
    Vec3 WorldCenter(BodyId id) const;

    /// Sets the linear and angular velocity. A static body is refused; a kinematic body keeps what is set (it is moved
    /// by it).
    ///
    /// - Param id: the body.
    /// - Param velocity: the new speed, finite.
    /// - Returns: `Ok`, `StaleId`, `Invalid` or `WrongType`.
    /// - Fails: as the return says.
    BodyStatus SetVelocity(BodyId id, const Velocity& velocity);

    /// Adds an impulse at a world point: the velocity changes at once by `impulse / mass` and the angular velocity by
    /// the torque it makes.
    ///
    /// - Param id: the body, dynamic.
    /// - Param impulse: newton seconds, finite.
    /// - Param point: where it acts, in the world, finite.
    /// - Returns: `Ok`, `StaleId`, `Invalid` or `WrongType`.
    /// - Fails: as the return says.
    BodyStatus ApplyImpulse(BodyId id, const Vec3& impulse, const Vec3& point);

    /// Adds a force at a world point, kept until the next step takes it.
    ///
    /// - Param id: the body, dynamic.
    /// - Param force: newtons, finite.
    /// - Param point: where it acts, in the world, finite.
    /// - Returns: `Ok`, `StaleId`, `Invalid` or `WrongType`.
    /// - Fails: as the return says.
    BodyStatus ApplyForce(BodyId id, const Vec3& force, const Vec3& point);

    /// Moves a body to a place without a step between.
    ///
    /// The orientation is set, the velocity is kept, and the teleport count grows so contact caches of the body are
    /// dropped.
    ///
    /// - Param id: the body.
    /// - Param position: the new origin, finite.
    /// - Param orientation: the new orientation, a unit quaternion.
    /// - Returns: `Ok`, `StaleId` or `Invalid`.
    /// - Fails: as the return says.
    BodyStatus SetTransform(BodyId id, const Vec3& position, const Quaternion& orientation);

    /// Recomputes a body's world inverse inertia from its orientation (the step does it after turning bodies).
    ///
    /// - Param body: the body to refresh.
    /// - Ownership: the body stays the caller's; nothing is kept.
    /// - Fails: never.
    static void RefreshInertia(Body* body);

    /// Returns what the world holds.
    ///
    /// - Returns: the counts.
    /// - Fails: never.
    PhysicsWorldStats Stats() const;

private:
    void RecomputeMass(Body* body);

    SlotPool<Body> bodies_;
    SlotPool<ColliderRecord> colliders_;
};

}  // namespace engine_placeholder::physics
