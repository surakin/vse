// The physics world (Phase 25 Day 376): the bodies and colliders of a simulation, with stable ids. The steps that move
// them come on Day 378.
#pragma once

#include "containers/unique_ptr.h"
#include "ecs/entity.h"
#include "math/matrix.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "physics/compound.h"
#include "physics/components.h"
#include "physics/hull.h"
#include "physics/ids.h"
#include "physics/mass.h"
#include "physics/slot_pool.h"
#include "physics/solver.h"
#include "physics/triangle_mesh.h"
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
    /// The origin and orientation before the last step, for the interpolated write-back (equal to the current ones
    /// after a teleport).
    Vec3 previous_position = Vec3(0.0f, 0.0f, 0.0f);
    /// The orientation before the last step.
    Quaternion previous_orientation = Quaternion::Identity();
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
    /// Whether the solver already integrated the body in the current step (cleared by the step).
    bool integrated = false;
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

/// How time is stepped.
///
/// - Thread-safety: thread-compatible plain data.
struct StepConfig
{
    /// Steps per second, from 1 to 1000 (the Day 373 spike: 30 to 120 Hz are stable; 60 is the default).
    f32 step_hz = 60.0f;
    /// The most steps one `Update` runs; a longer frame drops the rest (1 to 32).
    u32 max_substeps = 4;
    /// The gravity in metres per second squared; a body's `gravity_scale` multiplies it.
    Vec3 gravity = Vec3(0.0f, -9.81f, 0.0f);
    /// The contact margin in metres: shapes closer than this get a speculative contact, so a resting contact is stable
    /// and an approaching one is predicted.
    f32 contact_margin = 0.02f;
    /// Whether `Interpolated` blends the last two states (false: it returns the latest).
    bool interpolate = true;
    /// Solver iterations per substep (each is followed by a relax pass), 1 to 100.
    u32 velocity_iterations = 2;
    /// The penetration the solver leaves alone, in metres, 0 to 0.1.
    f32 contact_slop = 0.001f;
    /// An approach speed below this (metres per second) does not bounce.
    f32 restitution_threshold = 1.0f;
    /// Substeps per step for the bodies that touch: the solver integrates and solves each of them in turn, which keeps
    /// tall stacks still (1 to 16). A body that touches nothing takes one step.
    u32 substeps = 4;
    /// The stiffness of a resting contact, in hertz (the contact acts like a damped spring of this frequency; it is
    /// capped at a quarter of the substep rate).
    f32 contact_hertz = 30.0f;
    /// The damping ratio of that spring (above 1 it does not oscillate).
    f32 contact_damping_ratio = 10.0f;
    /// The fastest the solver pushes two overlapping shapes apart, in metres per second.
    f32 contact_push_velocity = 3.0f;
    /// A body is not allowed to move faster than this, in metres per second (it keeps its direction); it stops a body
    /// squeezed between planes from exploding.
    f32 max_velocity = 100.0f;
};

/// What stepping did.
///
/// - Thread-safety: thread-compatible plain data.
struct StepStats
{
    /// Fixed steps run in all.
    u64 steps = 0;
    /// Steps run by the last `Update`.
    u32 last_substeps = 0;
    /// `Update` calls that had more time than `max_substeps` steps could use: the rest was dropped.
    u64 dropped_updates = 0;
    /// The seconds dropped in all.
    f64 dropped_seconds = 0.0;
    /// Steps run while the process flushed denormals (the step's results are then not portable): zero in a correct
    /// build.
    u64 denormal_mode_steps = 0;
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

class BroadPhase;

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
    friend class ContactSolver;

public:
    /// Makes an empty world with the default step configuration.
    /// - Fails: never.
    PhysicsWorld();

    /// Destroys the world with its bodies, colliders and contact cache.
    /// - Fails: never.
    ~PhysicsWorld();

    /// Moves a world; the moved-from world is empty.
    /// - Fails: never.
    PhysicsWorld(PhysicsWorld&&) noexcept;

    /// Moves a world over this one.
    /// - Fails: never.
    PhysicsWorld& operator=(PhysicsWorld&&) noexcept;

    /// What the contact solver did in the last step.
    ///
    /// - Returns: the counters.
    /// - Fails: never.
    const SolverStats& GetSolverStats() const { return solver_.Stats(); }

    /// The number of contact points the solver remembers for warm starting.
    ///
    /// - Returns: the count.
    /// - Fails: never.
    u32 ContactCacheSize() const { return solver_.CacheSize(); }

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

    /// Sets how time is stepped.
    ///
    /// - Param config: the rate, the cap, the gravity and whether to interpolate.
    /// - Returns: `Ok`, or `Invalid` (a rate outside 1 to 1000, a cap outside 1 to 32, a gravity that is not finite)
    /// with the old config kept.
    /// - Fails: as the return says.
    BodyStatus SetStepConfig(const StepConfig& config);

    /// Returns how time is stepped.
    ///
    /// - Returns: the config.
    /// - Fails: never.
    const StepConfig& GetStepConfig() const { return step_config_; }

    /// Advances the world by a frame's time in fixed steps.
    ///
    /// It runs as many steps as the time holds, at most `max_substeps`, and keeps the rest for the next call. A frame
    /// with more time than the cap allows drops what the cap could not use and counts it, so a stall does not make the
    /// next frame longer (no spiral of death).
    ///
    /// - Param real_seconds: the frame's time, finite and not negative (anything else is ignored).
    /// - Returns: the number of steps run.
    /// - Fails: a time that is not finite or is negative runs no step.
    u32 Update(f32 real_seconds);

    /// Runs one fixed step now, whatever the accumulator holds.
    ///
    /// Forces and gravity change the velocities, damping slows them, then the positions and orientations move by
    /// semi-implicit Euler (the new velocity moves the body) and the forces are cleared. Static bodies do not move; a
    /// kinematic body moves by its velocity. The broad phase, narrow phase and solver join the step on later days.
    ///
    /// - Fails: never.
    void Step();

    /// Returns how far the accumulator is into the next step, from 0 to 1: the blend factor for `Interpolated`.
    ///
    /// - Returns: the fraction.
    /// - Fails: never.
    f32 Alpha() const;

    /// Returns where a body is drawn: the blend of its last two states by `alpha`.
    ///
    /// Positions are blended linearly,
    /// orientations by a normalized one along the short arc). With `interpolate` off, or right after a teleport, it is
    /// the current state.
    ///
    /// - Param id: the body.
    /// - Param alpha: from 0 (the previous state) to 1 (the current one), usually `Alpha()`.
    /// - Param position: receives the origin; not null.
    /// - Param orientation: receives the orientation; not null.
    /// - Returns: false for a stale id (the outputs are not written).
    /// - Ownership: the outputs are the caller's; nothing is kept.
    /// - Fails: as the return says.
    bool Interpolated(BodyId id, f32 alpha, Vec3* position, Quaternion* orientation) const;

    /// Returns what stepping did.
    ///
    /// - Returns: the counters.
    /// - Fails: never.
    StepStats GetStepStats() const { return step_stats_; }

    /// Hashes the state of every body in slot order.
    ///
    /// The words are the position, orientation, linear and angular velocity and the sleep
    /// byte, as `StateHasher` words. Two runs that agree to the bit hash alike; the hash is the cross-machine check of
    /// Phase 25.
    ///
    /// - Param valid: receives false if a number was not finite (may be null).
    /// - Returns: the hash.
    /// - Ownership: `valid` is the caller's; nothing is kept.
    /// - Fails: a non-finite number is reported through `valid`.
    u64 StateHash(bool* valid = nullptr) const;

    /// Returns the hulls that `Shape::MakeHull` shapes of this world refer to; add a hull here before a collider uses
    /// it.
    ///
    /// - Returns: the library.
    /// - Ownership: owned by the world.
    /// - Fails: never.
    HullLibrary& Hulls() { return hulls_; }

    /// Returns the meshes that `Shape::MakeMesh` shapes of this world refer to; add a mesh here before a collider uses
    /// it.
    ///
    /// - Returns: the library.
    /// - Ownership: owned by the world.
    /// - Fails: never.
    MeshLibrary& Meshes() { return meshes_; }

    /// Returns the mesh library, read only.
    ///
    /// - Returns: the library.
    /// - Ownership: owned by the world.
    /// - Fails: never.
    const MeshLibrary& Meshes() const { return meshes_; }

    /// Returns the compounds that `Shape::MakeCompound` shapes of this world refer to; add a compound here before a
    /// collider uses it.
    ///
    /// - Returns: the library.
    /// - Ownership: owned by the world.
    /// - Fails: never.
    CompoundLibrary& Compounds() { return compounds_; }

    /// Returns the compound library, read only.
    ///
    /// - Returns: the library.
    /// - Ownership: owned by the world.
    /// - Fails: never.
    const CompoundLibrary& Compounds() const { return compounds_; }

    /// Returns the hull library, read only.
    ///
    /// - Returns: the library.
    /// - Ownership: owned by the world.
    /// - Fails: never.
    const HullLibrary& Hulls() const { return hulls_; }

private:
    void RecomputeMass(Body* body);
    // The integration the step and the solver share: forces, gravity and damping change the velocity of a dynamic body;
    // the speed limit; and the position moves by the velocity (a dynamic body about its centre of mass, a kinematic one
    // about its origin).
    static void IntegrateVelocity(const Vec3& gravity, Body* body, f32 dt);
    static void LimitVelocity(f32 max_velocity, Body* body);
    static void IntegratePosition(Body* body, f32 dt);
    // A hull or mesh shape names an entry of its library, and its extent (the broad phase's bound) covers it; a mesh
    // belongs to a static body.
    bool HullKnown(const Shape& shape, BodyType body_type) const
    {
        if (shape.kind == ShapeKind::Mesh)
        {
            const TriangleMesh* mesh = meshes_.Find(shape.mesh);
            return body_type == BodyType::Static && mesh != nullptr &&
                   shape.half_extents.x >= mesh->Extent().x * 0.9999f &&
                   shape.half_extents.y >= mesh->Extent().y * 0.9999f &&
                   shape.half_extents.z >= mesh->Extent().z * 0.9999f;
        }
        if (shape.kind == ShapeKind::Compound)
        {
            const CompoundShape* compound = compounds_.Find(shape.compound);
            return compound != nullptr && shape.half_extents.x >= compound->Extent().x * 0.9999f &&
                   shape.half_extents.y >= compound->Extent().y * 0.9999f &&
                   shape.half_extents.z >= compound->Extent().z * 0.9999f;
        }
        if (shape.kind != ShapeKind::Hull)
        {
            return true;
        }
        const ConvexHull* hull = hulls_.Find(shape.hull);
        return hull != nullptr && shape.half_extents.x >= hull->Extent().x * 0.9999f &&
               shape.half_extents.y >= hull->Extent().y * 0.9999f && shape.half_extents.z >= hull->Extent().z * 0.9999f;
    }

    StepConfig step_config_;
    StepStats step_stats_;
    f64 accumulator_ = 0.0;
    SlotPool<Body> bodies_;
    SlotPool<ColliderRecord> colliders_;
    HullLibrary hulls_;
    MeshLibrary meshes_;
    CompoundLibrary compounds_;
    UniquePtr<BroadPhase> broad_;
    ContactSolver solver_;
};

}  // namespace engine_placeholder::physics
