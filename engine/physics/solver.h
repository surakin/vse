#pragma once

//! The contact solver of the physics module (Phase 25 Day 384): sequential impulses with warm starting. Contacts become
//! rows (a normal and two friction rows per point); the rows are solved in a fixed order, so the result does not depend
//! on the order the narrow phase produced the manifolds in. Only `+ - * /` and `Sqrt` are used.

#include "containers/span.h"
#include "containers/vector.h"
#include "math/vector.h"
#include "physics/contact.h"
#include "physics/ids.h"
#include "types.h"

namespace engine_placeholder::physics
{

class PhysicsWorld;

/// How a step is solved.
///
/// - Thread-safety: thread-compatible plain data.
struct SolverConfig
{
    /// Velocity iterations per step, 1 to 100.
    u32 iterations = 10;
    /// The fraction of the penetration beyond the slop that is removed per step as a velocity bias, 0 to 1.
    f32 bias = 0.2f;
    /// The penetration that is left alone, in metres, 0 to 0.1.
    f32 slop = 0.005f;
    /// An approach speed below this does not bounce, in metres per second.
    f32 restitution_threshold = 1.0f;
    /// The length of the step in seconds.
    f32 dt = 1.0f / 60.0f;
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
    /// Contact points solved.
    u32 points = 0;
    /// Points that started from a cached impulse.
    u32 warm_started = 0;
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
    /// Solves the contacts: builds the rows, warm starts, iterates and writes the new velocities into the bodies; then
    /// replaces the cache by this step's impulses.
    ///
    /// - Param world: the world whose bodies are changed.
    /// - Param inputs: the manifolds, in any order (they are solved in order of their collider ids).
    /// - Param config: the iteration count and the position correction.
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

    Vector<CacheEntry> cache_;
    Vector<CacheEntry> next_cache_;
    SolverStats stats_;
};

}  // namespace engine_placeholder::physics
