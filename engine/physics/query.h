// Queries over the physics world (Phase 25 Day 403): rays, shape casts, overlaps and closest points. They read the
// world as the last step left it and change nothing.
#pragma once

#include "containers/span.h"
#include "containers/vector.h"
#include "math/quaternion.h"
#include "math/vector.h"
#include "physics/contact.h"
#include "physics/ids.h"
#include "physics/shapes.h"
#include "physics/world.h"
#include "types.h"

namespace vse::physics
{

/// Which colliders a query looks at.
///
/// - Thread-safety: thread-compatible plain data.
struct PhysicsQueryFilter
{
    /// Layers to include, one bit per layer (0 to 31).
    u32 layer_mask = 0xFFFFFFFFu;
    /// The query's own group bits and the groups it hits, tested like two colliders' (`GroupsCollide`).
    u32 group = 0xFFFFFFFFu;
    /// The groups the query hits.
    u32 mask = 0xFFFFFFFFu;
    /// Whether trigger colliders are included.
    bool include_triggers = false;
    /// Whether colliders of static bodies are included.
    bool include_static = true;
    /// Whether colliders of kinematic bodies are included.
    bool include_kinematic = true;
    /// Whether colliders of dynamic bodies are included.
    bool include_dynamic = true;
    /// Bodies whose colliders are skipped (may be empty); borrowed for the call.
    Span<const BodyId> ignore;
};

/// The first (or each) thing a ray or a shape cast touched.
///
/// - Thread-safety: thread-compatible plain data.
struct PhysicsHit
{
    /// The collider hit.
    ColliderId collider;
    /// Its body.
    BodyId body;
    /// The point of contact, in the world.
    Vec3d point = Vec3d(0.0, 0.0, 0.0);
    /// The surface normal there, facing against the ray or the cast.
    Vec3 normal = Vec3(0.0f, 1.0f, 0.0f);
    /// How far along the ray or the cast the hit is (0 when the origin or the shape starts inside).
    f32 distance = 0.0f;
    /// The triangle for a mesh or height field collider, else `kNoTriangle`.
    u32 triangle = 0xFFFFFFFFu;
};

/// Casts a ray and returns the closest hit.
///
/// A ray that starts inside a solid collider hits it at distance zero with the normal opposite the ray. A mesh or a
/// height field has no inside (a ray from below the surface hits it from below). Ties go to the smaller collider key.
///
/// - Param world: the world.
/// - Param origin: where the ray starts.
/// - Param direction: any non-zero direction (it is normalized).
/// - Param max_distance: the length of the ray, above zero.
/// - Param filter: what to hit.
/// - Param hit: receives the closest hit (may be null).
/// - Returns: true if anything was hit.
/// - Thread-safety: thread-safe against other queries, while no step or change runs.
/// - Ownership: `hit` is the caller's; nothing is kept.
/// - Fails: a non-finite or zero input returns false.
bool RayCast(const PhysicsWorld& world, const Vec3d& origin, const Vec3& direction, f32 max_distance,
             const PhysicsQueryFilter& filter, PhysicsHit* hit);

/// Casts a ray at the height-field colliders only (terrain) and returns the closest hit; as [[physics::RayCast]]
/// otherwise.
///
/// - Param world: the world.
/// - Param origin: where the ray starts.
/// - Param direction: any non-zero direction (it is normalized).
/// - Param max_distance: the length of the ray, above zero.
/// - Param filter: what to hit.
/// - Param hit: receives the closest hit (may be null).
/// - Returns: true if a height field was hit.
/// - Thread-safety: thread-safe against other queries, while no step or change runs.
/// - Ownership: `hit` is the caller's; nothing is kept.
/// - Fails: a non-finite or zero input returns false.
bool TerrainRayCast(const PhysicsWorld& world, const Vec3d& origin, const Vec3& direction, f32 max_distance,
                    const PhysicsQueryFilter& filter, PhysicsHit* hit);

/// Finds the ground under a point: the highest surface of the height-field colliders that cover (`x`, `z`).
///
/// - Param world: the world.
/// - Param x: the world x of the point.
/// - Param z: the world z of the point.
/// - Param filter: what to consider.
/// - Param height: receives the surface's world y (may be null).
/// - Param normal: receives the surface's unit normal, pointing up (may be null).
/// - Returns: true if a height field covers the point.
/// - Thread-safety: thread-safe against other queries, while no step or change runs.
/// - Ownership: the outputs are the caller's.
/// - Fails: a non-finite coordinate, or no field under the point, returns false.
bool TerrainHeight(const PhysicsWorld& world, f64 x, f64 z, const PhysicsQueryFilter& filter, f64* height,
                   Vec3* normal);

/// Casts a ray and returns every hit, nearest first (ties by collider key).
///
/// - Param world: the world.
/// - Param origin: where the ray starts.
/// - Param direction: any non-zero direction.
/// - Param max_distance: the length of the ray.
/// - Param filter: what to hit.
/// - Param hits: receives the hits, one per collider (the collider's first); cleared first.
/// - Returns: the number of hits.
/// - Thread-safety: thread-safe against other queries, while no step or change runs.
/// - Ownership: `hits` is the caller's.
/// - Fails: a bad input returns zero hits.
usize RayCastAll(const PhysicsWorld& world, const Vec3d& origin, const Vec3& direction, f32 max_distance,
                 const PhysicsQueryFilter& filter, Vector<PhysicsHit>* hits);

/// Sweeps a shape along a direction and returns the first collider it touches.
///
/// The time of impact is found by sampling the sweep and bisecting the first overlapping interval, to about a
/// micrometre; the shape does not turn. A shape that starts touching a collider hits it at distance zero.
///
/// - Param world: the world.
/// - Param shape: a sphere, box, capsule or hull of the world's library (not a mesh or a compound).
/// - Param position: where the shape's origin starts.
/// - Param rotation: its orientation.
/// - Param direction: any non-zero direction.
/// - Param distance: how far to sweep.
/// - Param filter: what to hit.
/// - Param hit: receives the hit; `distance` is how far the shape moved before touching (may be null).
/// - Returns: true if anything was hit.
/// - Thread-safety: thread-safe against other queries, while no step or change runs.
/// - Ownership: `hit` is the caller's.
/// - Fails: an invalid shape or input returns false.
bool ShapeCast(const PhysicsWorld& world, const Shape& shape, const Vec3d& position, const Quaternion& rotation,
               const Vec3& direction, f32 distance, const PhysicsQueryFilter& filter, PhysicsHit* hit);

/// Finds the colliders a shape touches at a pose.
///
/// - Param world: the world.
/// - Param shape: the shape (any kind of the library).
/// - Param position: where its origin is.
/// - Param rotation: its orientation.
/// - Param filter: what to include.
/// - Param out: receives the colliders sorted by id key; cleared first.
/// - Returns: the number found.
/// - Thread-safety: thread-safe against other queries, while no step or change runs.
/// - Ownership: `out` is the caller's.
/// - Fails: an invalid shape returns zero.
usize Overlap(const PhysicsWorld& world, const Shape& shape, const Vec3d& position, const Quaternion& rotation,
              const PhysicsQueryFilter& filter, Vector<ColliderId>* out);

/// Finds the point on a collider's surface nearest to a point.
///
/// - Param world: the world.
/// - Param point: the query point.
/// - Param max_distance: colliders farther than this are ignored.
/// - Param filter: what to include.
/// - Param hit: receives the nearest surface point (`point`), the direction from the surface to the query point
///   (`normal`) and the distance (zero when the point is inside; may be null).
/// - Returns: true if a collider lies within `max_distance`.
/// - Thread-safety: thread-safe against other queries, while no step or change runs.
/// - Ownership: `hit` is the caller's.
/// - Fails: a bad input returns false.
bool ClosestPoint(const PhysicsWorld& world, const Vec3d& point, f32 max_distance, const PhysicsQueryFilter& filter,
                  PhysicsHit* hit);

}  // namespace vse::physics
