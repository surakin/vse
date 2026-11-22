#pragma once

//! The contact solver of the physics module: sequential impulses with warm starting. Contacts become
//! rows (a normal and two friction rows per point); the rows are solved in a fixed order, so the result does not depend
//! on the order the narrow phase produced the manifolds in. Only `+ - * /` and `Sqrt` are used.

#include "containers/span.h"
#include "containers/vector.h"
#include "math/vector.h"
#include "physics/contact.h"
#include "physics/ids.h"
#include "types.h"

namespace vse::physics
{

class PhysicsWorld;
class SnapshotOut;
class SnapshotIn;

/// How a step is solved.
///
/// - Thread-safety: thread-compatible plain data.
struct SolverConfig
{
    /// Substeps the touching bodies take, 1 to 16.
    u32 substeps = 4;
    /// Solver iterations per substep (and as many relax iterations), 1 to 100.
    u32 iterations = 2;
    /// The stiffness of a resting contact in hertz (capped at a quarter of the substep rate); static contacts are twice
    /// as stiff.
    f32 contact_hertz = 30.0f;
    /// The damping ratio of the contact spring.
    f32 damping_ratio = 10.0f;
    /// The fastest an overlap is pushed apart, in metres per second.
    f32 push_velocity = 3.0f;
    /// The penetration that is left alone, in metres, 0 to 0.1.
    f32 slop = 0.001f;
    /// An approach speed below this does not bounce, in metres per second.
    f32 restitution_threshold = 1.0f;
    /// The length of the step in seconds.
    f32 dt = 1.0f / 60.0f;
    /// The gravity the bodies that touch fall with.
    Vec3 gravity = Vec3(0.0f, -9.81f, 0.0f);
    /// The speed limit of a body, in metres per second.
    f32 max_velocity = 100.0f;
    /// The stiffness of a joint's constraint in hertz (capped at a quarter of the substep rate).
    f32 joint_hertz = 60.0f;
    /// The damping ratio of the joint spring.
    f32 joint_damping_ratio = 2.0f;
};

/// One manifold to solve: the two colliders and bodies and the contact, with the combined friction and restitution in
/// the manifold.
///
/// - Thread-safety: thread-compatible plain data.
struct ContactInput
{
    /// Collider A (the manifold's normal points from A to B).
    ColliderId a;
    /// Collider B.
    ColliderId b;
    /// The body of A.
    BodyId body_a;
    /// The body of B.
    BodyId body_b;
    /// The contact.
    ContactManifold manifold;
};

/// What the last solve did.
///
/// - Thread-safety: thread-compatible plain data.
struct SolverStats
{
    /// Manifolds solved.
    u32 manifolds = 0;
    /// Joints solved.
    u32 joints = 0;
    /// Contact points solved.
    u32 points = 0;
    /// Points that started from a cached impulse.
    u32 warm_started = 0;
    /// Solver iterations run in the step (substeps times iterations, relax passes not counted); 0 without a manifold.
    u32 iterations = 0;
    /// The deepest penetration of the step at its start, in metres (0 when nothing penetrates).
    f32 max_penetration = 0.0f;
    /// The largest normal impulse any point received in one substep.
    f32 max_impulse = 0.0f;
};

/// The solver and its contact cache.
///
/// The cache holds the last step's impulses by (collider A, collider B, feature id) in sorted storage; a point whose
/// key is found, whose body has not been teleported and whose place on body A moved less than 5 cm starts from the
/// cached impulse.
///
/// - Thread-safety: thread-compatible; one thread solves at a time.
/// - Ownership: owns its cache and scratch storage.
class ContactSolver
{
public:
    /// Solves the contacts and the joints of the world and integrates the bodies that touch (each is marked
    /// `integrated`): the rows are built from the manifolds once, then every substep integrates the velocities, applies
    /// the cached impulses, solves with the position feedback of a soft constraint, moves the bodies and solves again
    /// without it (the relax pass); a last pass applies restitution. The cache is replaced by this step's impulses.
    ///
    /// - Param world: the world whose bodies are changed.
    /// - Param inputs: the manifolds, in any order (they are solved in order of their collider ids).
    /// - Param config: the substeps, iterations and softness.
    /// - Ownership: `inputs` is read during the call only.
    /// - Fails: a manifold whose body is not found is skipped.
    void Solve(PhysicsWorld* world, Span<const ContactInput> inputs, const SolverConfig& config);

    /// Forgets the cached impulses of a body (it was teleported or destroyed).
    ///
    /// - Param body: the body.
    /// - Fails: never.
    void ForgetBody(BodyId body);

    /// Empties the cache.
    /// - Fails: never.
    void Clear();

    /// The number of cached points.
    /// - Fails: never.
    u32 CacheSize() const { return static_cast<u32>(cache_.Size()); }

    /// The total normal impulse the cache holds for the points of one pair of colliders (the last solve's).
    ///
    /// - Param a: the collider with the smaller key.
    /// - Param b: the other collider.
    /// - Returns: the sum, 0 for a pair with no cached point.
    /// - Fails: never.
    f32 PairImpulse(ColliderId a, ColliderId b) const;

    /// Writes the contact cache (for a snapshot).
    ///
    /// - Param out: the writer.
    /// - Ownership: borrows the writer for the call.
    /// - Fails: never.
    void SaveCache(SnapshotOut* out) const;

    /// Replaces the contact cache by one read from a snapshot.
    ///
    /// - Param in: the reader.
    /// - Returns: false when the data is not valid (the cache is then empty).
    /// - Ownership: borrows the reader for the call.
    /// - Fails: as the return says.
    bool LoadCache(SnapshotIn* in);

    /// What the last `Solve` did.
    /// - Fails: never.
    const SolverStats& Stats() const { return stats_; }

private:
    struct CacheEntry
    {
        u64 key_a = 0;
        u64 key_b = 0;
        u32 feature = 0;
        BodyId body_a;
        BodyId body_b;
        u32 teleports_a = 0;
        u32 teleports_b = 0;
        Vec3 local_a = Vec3(0.0f, 0.0f, 0.0f);  // the point in body A's frame
        f32 normal_impulse = 0.0f;
        Vec3 tangent_impulse = Vec3(0.0f, 0.0f, 0.0f);  // in the world
    };

    struct Row;
    struct Constraint;
    struct BodyState;

    Vector<CacheEntry> cache_;
    Vector<CacheEntry> next_cache_;
    SolverStats stats_;
};

}  // namespace vse::physics
