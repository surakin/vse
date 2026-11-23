// The physics world (Phase 25 Day 388): the bodies and colliders of a simulation, with stable ids. The steps that move
// them come on Day 390.
#pragma once

#include "containers/unique_ptr.h"
#include "ecs/entity.h"
#include "math/matrix.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "physics/compound.h"
#include "physics/events.h"
#include "physics/components.h"
#include "physics/hull.h"
#include "physics/ids.h"
#include "physics/joint.h"
#include "physics/mass.h"
#include "physics/slot_pool.h"
#include "physics/snapshot_status.h"
#include "physics/solver.h"
#include "physics/height_field.h"
#include "physics/triangle_mesh.h"
#include "types.h"

namespace vse::jobs
{
class Scheduler;
}

namespace vse::bytes
{
class ByteWriter;
}

namespace vse::physics
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
    Unsupported,  ///< Not built yet (a second collider on a body).
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
    Vec3d position = Vec3d(0.0, 0.0, 0.0);
    /// How it is turned (a unit quaternion).
    Quaternion orientation = Quaternion::Identity();
    /// Its speed.
    Velocity velocity;
    /// The ECS entity the body belongs to (invalid for a body made by hand).
    ecs::Entity entity;
};

/// One body: its state and mass. The world's accessors are the way to change it; the step reads and writes
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
    Vec3d position = Vec3d(0.0, 0.0, 0.0);
    /// The body's orientation.
    Quaternion orientation = Quaternion::Identity();
    /// The origin and orientation before the last step, for the interpolated write-back (equal to the current ones
    /// after a teleport).
    Vec3d previous_position = Vec3d(0.0, 0.0, 0.0);
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
    /// Whether the body is asleep: it is neither integrated nor solved until something wakes it.
    bool sleeping = false;
    /// Seconds the body has stayed below the sleep thresholds.
    f32 sleep_time = 0.0f;
    /// The island the body was in at the end of the last step: the slot of the island's lowest body plus one, or zero
    /// for a body that is not dynamic. A sleeping island keeps its number.
    u32 island = 0;
    /// Whether the body was changed from outside since the last step (moved, resized, retyped): for a body that is not
    /// dynamic this makes it wake what it touches, like a moving one.
    bool disturbed = false;
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
    /// Steps per second, from 1 to 1000 (30 to 120 Hz are stable; 60 is the default).
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
    /// The stiffness of a joint's constraint as a spring, in hertz (capped at a quarter of the substep rate).
    f32 joint_hertz = 60.0f;
    /// The damping ratio of that spring.
    f32 joint_damping_ratio = 2.0f;
    /// A body that travels more than this fraction of its collider's smallest extent in one step is swept by
    /// continuous collision against static and kinematic bodies, even without `kBodyContinuous` (0 = only the flag).
    f32 ccd_threshold = 0.0f;
    /// How many threads the step may use: 1 runs everything inline, more uses the scheduler given to `SetScheduler`
    /// (its own participant count decides how many run). The results are the same to the bit for every value.
    u32 threads = 1;
    /// Whether bodies may fall asleep at all (a body can still opt out with `kBodyNoSleep`).
    bool allow_sleep = true;
    /// A body is at rest while its linear speed is below this, in metres per second.
    f32 sleep_linear = 0.05f;
    /// A body is at rest while its angular speed is below this, in radians per second.
    f32 sleep_angular = 0.05f;
    /// An island falls asleep once every body in it has been at rest this many seconds.
    f32 sleep_time = 0.5f;
    /// Whether a pair that keeps touching reports a `Stay` event every step (off: only begin and end).
    bool stay_events = false;
    /// Whether the program draws the world for debugging (`DebugDraw`); the world itself does nothing with it.
    bool debug_draw = false;
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
    /// Joints removed because they carried more than their break force or torque.
    u64 joints_broken = 0;
    /// Steps run while the process flushed denormals (the step's results are then not portable): zero in a correct
    /// build.
    u64 denormal_mode_steps = 0;
    /// Continuous sweeps made against one static or kinematic collider, in all (zero when no body is continuous).
    u64 ccd_queries = 0;
    /// Sweeps that found a hit and moved a body back to its time of impact, in all.
    u64 ccd_hits = 0;
    /// Islands (groups of dynamic bodies tied by contacts and joints) after the last step, awake and asleep.
    u32 islands = 0;
    /// Dynamic bodies asleep after the last step.
    u32 sleeping_bodies = 0;
    /// Islands that fell asleep, in all.
    u64 islands_slept = 0;
    /// Bodies woken, in all (by contact, joint, force, impulse, teleport, change or removal).
    u64 bodies_woken = 0;
    /// Wall-clock nanoseconds spent in all steps so far, by part (timing only: not state, not in snapshots or hashes):
    /// the broad phase, the narrow phase with its bookkeeping, the solver, the free bodies' integration and the work
    /// after it (events, continuous sweeps, sleeping, joint breaks).
    u64 broad_ns = 0;
    /// See `broad_ns`: the narrow phase.
    u64 narrow_ns = 0;
    /// See `broad_ns`: the solver.
    u64 solve_ns = 0;
    /// See `broad_ns`: the integration of the bodies the solver did not take.
    u64 integrate_ns = 0;
    /// See `broad_ns`: the events, continuous sweeps, sleeping and joint breaks.
    u64 after_ns = 0;
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
    /// Joint slots ever made.
    u32 joint_slots = 0;
};

struct ColliderPair;
class BroadPhase;
class QueryIndex;

/// Everything a world can say about itself, in one value.
///
/// - Thread-safety: thread-compatible plain data.
struct PhysicsStats
{
    /// Bodies and colliders.
    PhysicsWorldStats world;
    /// The stepping counters.
    StepStats step;
    /// What the last solve did.
    SolverStats solver;
    /// Joints alive.
    u32 joints = 0;
    /// Pairs of colliders that touch now.
    u32 contacts = 0;
    /// Events of the last update.
    u32 events = 0;
    /// Pairs of colliders whose boxes overlap (the broad phase's pairs).
    u32 broad_pairs = 0;
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
    /// A body takes one collider for now.
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
    /// - Returns: the body, or null for a stale id. The pointer stays valid until the body is destroyed. Its pose is
    ///   changed only through `SetTransform` (and its class through `SetSettings`): the queries' index follows those
    ///   calls, not writes through the pointer.
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
    /// - Returns: the record, or null for a stale id. Its shape and place are changed only through `SetCollider`: the
    ///   queries' index follows that call, not writes through the pointer.
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

    /// The id of the joint now in a slot (its generation is 0, so it is not valid, when the slot is free).
    ///
    /// - Param index: below `Stats().joint_slots`.
    /// - Returns: the id.
    /// - Fails: never.
    JointId JointAtSlot(u32 index) const;

    /// Returns the world position of a body's centre of mass.
    ///
    /// - Param id: the body.
    /// - Returns: the point; the origin for a stale id.
    /// - Fails: never.
    Vec3d WorldCenter(BodyId id) const;

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
    BodyStatus ApplyImpulse(BodyId id, const Vec3& impulse, const Vec3d& point);

    /// Adds a force at a world point, kept until the next step takes it.
    ///
    /// - Param id: the body, dynamic.
    /// - Param force: newtons, finite.
    /// - Param point: where it acts, in the world, finite.
    /// - Returns: `Ok`, `StaleId`, `Invalid` or `WrongType`.
    /// - Fails: as the return says.
    BodyStatus ApplyForce(BodyId id, const Vec3& force, const Vec3d& point);

    /// Makes a joint between two bodies (or a body and the world).
    ///
    /// - Param desc: the joint; `desc.Validate()` must be `Ok`, body A must exist and be a body (not static) and so
    /// must body B unless it is null (the world); at least one of them must be dynamic.
    /// - Param status: receives why it failed (may be null).
    /// - Returns: the id, or the null id when refused.
    /// - Ownership: the world owns the joint; destroying either body destroys it.
    /// - Fails: the null id, with `status` (`StaleId` for a body that is gone, `Invalid` for refused numbers or two
    /// bodies that cannot move).
    JointId CreateJoint(const JointDesc& desc, BodyStatus* status = nullptr);

    /// Destroys a joint.
    ///
    /// - Param id: the joint.
    /// - Returns: `StaleId` if there is no such joint.
    /// - Fails: as the return says.
    BodyStatus DestroyJoint(JointId id);

    /// Finds a joint.
    ///
    /// - Param id: the joint.
    /// - Returns: the record, or null for a stale id.
    /// - Ownership: owned by the world; valid until the joint is destroyed or the step removes it.
    /// - Fails: a stale id returns null.
    const JointRecord* FindJoint(JointId id) const;

    /// The number of joints.
    /// - Fails: never.
    u32 JointCount() const { return joints_.Live(); }

    /// The force and torque a joint carried in the last step.
    ///
    /// - Param id: the joint.
    /// - Param force: receives the force on body B in newtons (may be null).
    /// - Param torque: receives the torque on body B in newton metres (may be null).
    /// - Ownership: the outputs are the caller's; nothing is kept.
    /// - Returns: `StaleId` if there is no such joint.
    /// - Fails: as the return says.
    BodyStatus JointReaction(JointId id, Vec3* force, Vec3* torque) const;

    /// Describes a ball joint at a world point: the anchors are the point seen from each body.
    ///
    /// - Param a: the first body.
    /// - Param b: the second body, or null for the world.
    /// - Param world_point: where the joint is.
    /// - Returns: the description (unchanged defaults when a body is stale).
    /// - Fails: stale bodies leave the anchors at zero.
    JointDesc MakeBallJoint(BodyId a, BodyId b, const Vec3d& world_point) const;

    /// Describes a rigid distance joint between two world points on the bodies, with the length they have now.
    ///
    /// - Param a: the first body.
    /// - Param b: the second body, or null for the world.
    /// - Param point_a: the anchor on body A, in the world.
    /// - Param point_b: the anchor on body B, in the world.
    /// - Returns: the description.
    /// - Fails: stale bodies leave the anchors at zero.
    JointDesc MakeDistanceJoint(BodyId a, BodyId b, const Vec3d& point_a, const Vec3d& point_b) const;

    /// Describes a fixed joint at a world point that keeps the bodies' orientations relative to each other as they are.
    ///
    /// - Param a: the first body.
    /// - Param b: the second body, or null for the world.
    /// - Param world_point: where the joint is.
    /// - Returns: the description.
    /// - Fails: stale bodies leave the anchors at zero.
    JointDesc MakeFixedJoint(BodyId a, BodyId b, const Vec3d& world_point) const;

    /// Describes a hinge at a world point about a world axis, at rest now (its angle is zero), with no limit, motor or
    /// spring: set them in the description.
    ///
    /// - Param a: the first body.
    /// - Param b: the second body, or null for the world.
    /// - Param world_point: where the hinge is.
    /// - Param world_axis: the axis the bodies turn about, non-zero (normalized here).
    /// - Returns: the description.
    /// - Fails: stale bodies leave the anchors at zero; a zero axis becomes Y.
    JointDesc MakeHingeJoint(BodyId a, BodyId b, const Vec3d& world_point, const Vec3& world_axis) const;

    /// The angle of a hinge, in radians, counted across full turns.
    ///
    /// - Param id: the joint.
    /// - Param angle: receives the angle.
    /// - Ownership: `angle` is the caller's; nothing is kept.
    /// - Returns: `StaleId` if there is no such joint, `WrongType` if it is not a hinge.
    /// - Fails: as the return says.
    BodyStatus JointAngle(JointId id, f32* angle) const;

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
    BodyStatus SetTransform(BodyId id, const Vec3d& position, const Quaternion& orientation);

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

    /// A function the world calls at the start of every fixed step, before gravity and the solver, with the step's
    /// length: a program's own per-step work (the character controllers' moves).
    using PreStepHook = void (*)(void* user, PhysicsWorld* world, f32 dt);

    /// Sets (or, with a null `hook`, removes) the function called at the start of every fixed step, whether `Update` or
    /// `Step` runs it. The hook may move and change bodies; it must not step the world or destroy the hook. It is not
    /// part of a snapshot: a restored world has none.
    ///
    /// - Param hook: the function, or null.
    /// - Param user: passed to it; borrowed, it must outlive the hook.
    /// - Ownership: nothing is kept but the two pointers.
    /// - Fails: never.
    void SetPreStepHook(PreStepHook hook, void* user)
    {
        pre_step_ = hook;
        pre_step_user_ = user;
    }

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
    bool Interpolated(BodyId id, f32 alpha, Vec3d* position, Quaternion* orientation) const;

    /// Gives the step a job scheduler for its parallel parts (the narrow phase and the integration of free bodies);
    /// without one, or with `StepConfig::threads` of 1, the step runs inline. The result does not depend on either.
    ///
    /// - Param scheduler: the scheduler, or null; it must outlive its use by the world. `Step` and `Update` must then
    ///   be called from the scheduler's main thread, outside any job.
    /// - Thread-safety: owner-thread-only. Call between steps.
    /// - Ownership: the scheduler is borrowed.
    /// - Fails: never.
    void SetScheduler(jobs::Scheduler* scheduler) { scheduler_ = scheduler; }

    /// Tells whether a body is asleep.
    ///
    /// - Returns: true if the body exists and sleeps.
    /// - Fails: a stale id returns false.
    bool IsSleeping(BodyId id) const
    {
        const Body* body = Find(id);
        return body != nullptr && body->sleeping;
    }

    /// Returns the island of a body as of the last step: bodies with the same number touch each other, directly or
    /// through other bodies or joints. The number is the slot of the island's lowest body plus one, so it does not
    /// depend on the order the bodies were visited.
    ///
    /// - Param island: receives the number, or zero for a body that is static or kinematic or not yet stepped.
    /// - Returns: `Ok`, or `StaleId`.
    /// - Ownership: `island` is the caller's; nothing is kept.
    /// - Fails: as the return says.
    BodyStatus IslandOf(BodyId id, u32* island) const;

    /// Wakes a body and every body of its island; it also restarts the body's rest timer.
    ///
    /// - Returns: `Ok`, or `StaleId`.
    /// - Fails: as the return says.
    BodyStatus Wake(BodyId id);

    /// Puts a dynamic body to sleep, and every body of its island: their velocities are zeroed and nothing moves them
    /// until something wakes them (a force, an impulse, a body that touches them moving, `Wake`). A body that has not
    /// been stepped yet has no island and sleeps alone. A body that is not dynamic is left as it is.
    ///
    /// - Param id: the body.
    /// - Returns: `Ok`, `StaleId`, or `WrongType` for a static or kinematic body.
    /// - Fails: as the return says.
    BodyStatus Sleep(BodyId id);

    /// Returns what stepping did.
    ///
    /// - Returns: the counters.
    /// - Fails: never.
    StepStats GetStepStats() const { return step_stats_; }

    /// Writes the whole simulation state: the step configuration, the time accumulator, every body, collider and
    /// joint with its slot (so ids made later come out the same), the contact cache, the touching pairs and the step
    /// count. The format is the canonical encoding of the storage module (little-endian, bit patterns, varint counts),
    /// begins with a magic and a version, and is the same on every platform. Shapes name hulls, meshes and compounds by
    /// id: the libraries are content, not state, and are not written (a restore needs a world that holds them). The
    /// events, the broad phase and the statistics are not state: they are rebuilt.
    ///
    /// - Param out: the writer the bytes are appended to.
    /// - Ownership: nothing is kept.
    /// - Fails: never.
    void Snapshot(bytes::ByteWriter* out) const;

    /// Replaces the world's state by a snapshot's: the bodies, colliders, joints, contact cache and configuration of
    /// the world are dropped and the snapshot's put in their place. The libraries and the scheduler stay. A run
    /// restored from a snapshot continues bit for bit as the original did.
    ///
    /// Every value is checked (counts against the bytes left, ids against slots, numbers for being finite, shapes
    /// against the libraries, chains of colliders against their bodies, the configuration against `SetStepConfig`), so
    /// truncated, corrupt or hostile bytes are refused without a crash.
    ///
    /// - Param data: the bytes `Snapshot` wrote.
    /// - Returns: `Ok`, or why not; on a refusal the world is unchanged.
    /// - Ownership: `data` is read during the call only.
    /// - Fails: as the return says.
    SnapshotStatus Restore(ByteSpan data);

    /// Everything the world counts, in one value.
    ///
    /// - Returns: the counters.
    /// - Fails: never.
    PhysicsStats AllStats() const;

    /// The events of the last `Update` (or of the last `Step` called by hand): contact begin, stay and end, trigger
    /// enter and exit, joint breaks, in the order of the steps and of the collider keys. Destroying a body removes the
    /// pending events that name it.
    ///
    /// - Returns: the lists, valid until the next `Update`, `Step` or `ClearEvents`.
    /// - Ownership: owned by the world.
    /// - Fails: never.
    const PhysicsEvents& Events() const { return events_; }

    /// Empties the event lists. `Update` does it at its start and `Step` at its start (unless `Update` called it), so a
    /// program that reads the events after each call does not need this.
    ///
    /// - Fails: never.
    void ClearEvents();

    /// The pairs of colliders that touch now, sorted by collider key, with the manifold and impulse of the last step:
    /// the view a system reads when it wants the contacts and not only the changes.
    ///
    /// - Returns: the records, valid until the next step or until a body is destroyed.
    /// - Ownership: owned by the world.
    /// - Fails: never.
    const Vector<ContactRecord>& Contacts() const { return contacts_; }

    /// The index the queries ask for candidates (`RayCast`, `ShapeCast`, `Overlap`, ...); null before the first
    /// collider.
    ///
    /// - Internal: the queries' own path; not for callers.
    /// - Ownership: owned by the world; valid until the next change of a collider, a step or a restore.
    const QueryIndex* QueryIndexOf() const { return query_index_enabled_ ? query_index_.Get() : nullptr; }

    /// Turns the queries' use of the index off and on; while off, every query tests every collider, which is the
    /// reference the index's results are compared with. The index is kept current either way.
    ///
    /// - Internal: for the tests that compare the two paths.
    /// - Param enabled: whether the queries use the index (the default).
    void SetQueryIndexEnabled(bool enabled) { query_index_enabled_ = enabled; }

    /// Hashes the state of every body in slot order.
    ///
    /// The words are the position, orientation, linear and angular velocity and the sleep
    /// byte, as `StateHasher` words. Two runs that agree to the bit hash alike; the hash is the cross-machine check.
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

    /// Returns the height fields that `Shape::MakeHeightField` shapes of this world refer to; add a field here before
    /// a collider uses it.
    ///
    /// - Returns: the library.
    /// - Ownership: owned by the world.
    /// - Fails: never.
    HeightFieldLibrary& HeightFields() { return height_fields_; }

    /// Returns the height field library, read only.
    ///
    /// - Returns: the library.
    /// - Ownership: owned by the world.
    /// - Fails: never.
    const HeightFieldLibrary& HeightFields() const { return height_fields_; }

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
    // Island and sleep work (island.cpp). WakeIsland wakes the body's whole island and restarts its timer;
    // WakeNeighbours wakes the sleeping bodies the last step's pairs tie to a body that is not dynamic (they are not
    // in its island); UpdateSleep builds the islands from the contact edges (slot pairs) and the joints, advances the
    // rest timers and puts the islands that rested long enough to sleep.
    void WakeIsland(Body* body);
    void WakeNeighbours(BodyId id);
    // A change from outside (a move, a new collider, a new class, the body's removal): wakes the body's island, marks
    // the body disturbed and, for a body that is not dynamic, wakes what sleeps against it.
    void Disturb(Body* body);
    void UpdateSleep(const Vector<u32>& edges, f32 dt);
    // Continuous collision (ccd.cpp): after the step's motion, a continuous body that passed into a static or kinematic
    // collider goes back to the time of impact.
    void SweepContinuous();
    // Whether the body moves things it touches this step: a dynamic body that is awake, a kinematic body with a
    // velocity, and any body changed from outside.
    static bool Disturbing(const Body& body);
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
        if (shape.kind == ShapeKind::HeightField)
        {
            const HeightField* field = height_fields_.Find(shape.height_field);
            return body_type == BodyType::Static && field != nullptr &&
                   shape.half_extents.x >= field->Extent().x * 0.9999f &&
                   shape.half_extents.y >= field->Extent().y * 0.9999f &&
                   shape.half_extents.z >= field->Extent().z * 0.9999f;
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
    jobs::Scheduler* scheduler_ = nullptr;
    StepStats step_stats_;
    f64 accumulator_ = 0.0;
    SlotPool<Body> bodies_;
    SlotPool<ColliderRecord> colliders_;
    SlotPool<JointRecord> joints_;
    HullLibrary hulls_;
    MeshLibrary meshes_;
    HeightFieldLibrary height_fields_;
    CompoundLibrary compounds_;
    UniquePtr<BroadPhase> broad_;
    // Every collider's world box, for the queries (query_index.h); made by the first collider, kept current by every
    // change of a collider's body, shape or class.
    UniquePtr<QueryIndex> query_index_;
    bool query_index_enabled_ = true;
    void RefreshQueryIndex(ColliderId id);
    ContactSolver solver_;
    // Events (Day 405): the lists of the last update, the pairs that touch (contacts) and the trigger pairs that
    // overlap, both sorted by collider key; `keep_events_` is set while `Update` runs its steps.
    PhysicsEvents events_;
    Vector<ContactRecord> contacts_;
    Vector<TriggerEvent> triggers_touching_;
    bool keep_events_ = false;
    PreStepHook pre_step_ = nullptr;
    void* pre_step_user_ = nullptr;
    void UpdateContactEvents(const Vector<ContactInput>& inputs, const Vector<ColliderPair>& carried);
    void UpdateTriggerEvents(const Vector<ColliderPair>& pairs);
    void DropEventsOf(BodyId id);
};

}  // namespace vse::physics
