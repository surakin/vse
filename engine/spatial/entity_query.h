#pragma once

//! Entity-facing queries: the questions of `spatial/query.h` asked of a named set of a
//! [[spatial::SpatialWorld]] and answered in entities, a join with component access, and ray and overlap batches that
//! run as jobs. A key is `Entity::ToU64`, which is how the sync adds entities.

#include "containers/span.h"
#include "containers/vector.h"
#include "ecs/entity.h"
#include "ecs/world.h"
#include "jobs/scheduler.h"
#include "lang/traits.h"
#include "log/assert.h"
#include "math/geometry.h"
#include "spatial/query.h"
#include "spatial/spatial_world.h"
#include "types.h"

namespace vse::spatial
{

/// A ray or sweep hit on an entity.
///
/// - Thread-safety: thread-compatible plain data.
struct EntityHit
{
    /// The entity.
    ecs::Entity entity;
    /// The parameter along the ray (units of the direction's length), or the fraction of the sweep's motion.
    f64 t = 0.0f;
    /// The point where the ray met the box (`origin + direction * t`); for a sweep, the box's center at `t`.
    Vec3d point = Vec3d(0, 0, 0);
};

/// An entity and its distance from a query point.
///
/// - Thread-safety: thread-compatible plain data.
struct EntityDistance
{
    /// The entity.
    ecs::Entity entity;
    /// The distance from the point to the entity's box (0 when the point is inside).
    f64 distance = 0.0f;
};

/// The entities of a set whose boxes overlap `box`, sorted by entity id.
///
/// The entities are those of the last sync; one destroyed since may still be listed until the next sync (the same rule
/// as a pointer from `World::Get`: valid until the next structural change; check `World::Exists` when in doubt).
///
/// - Param spatial: the world's sets.
/// - Param set: the set to ask.
/// - Param box: the region.
/// - Param filter: the layer mask and key filter.
/// - Param out: receives the entities (cleared first).
/// - Returns: how many were found; 0 for a set id that names none.
/// - Fails: a set id that names none finds nothing.
/// - Thread-safety: thread-safe against other queries; not against the sync.
/// - Ownership: `out` is borrowed for the call.
usize OverlapEntities(const SpatialWorld& spatial, SetId set, const Aabbd& box, const QueryFilter& filter,
                      Vector<ecs::Entity>* out);

/// The nearest entity a ray meets within `max_t`.
///
/// - Param spatial: the world's sets.
/// - Param set: the set to ask.
/// - Param ray: the ray (see [[spatial::Raycast]] for the input rules).
/// - Param max_t: the longest parameter to look at.
/// - Param filter: the layer mask and key filter.
/// - Param hit: receives the hit when there is one.
/// - Returns: whether there was a hit.
/// - Fails: a bad ray, a negative `max_t` or a set id that names none finds nothing.
/// - Thread-safety: thread-safe against other queries; not against the sync.
/// - Ownership: `hit` is borrowed for the call.
bool RaycastEntity(const SpatialWorld& spatial, SetId set, const Rayd& ray, f64 max_t, const QueryFilter& filter,
                   EntityHit* hit);

/// The first entity a box meets while moving by `motion` (see [[spatial::SweepBox]]).
///
/// - Param spatial: the world's sets.
/// - Param set: the set to ask.
/// - Param box: the moving box.
/// - Param motion: how far it moves.
/// - Param filter: the layer mask and key filter.
/// - Param hit: receives the hit when there is one.
/// - Returns: whether there was a hit.
/// - Fails: a set id that names none finds nothing.
/// - Thread-safety: thread-safe against other queries; not against the sync.
/// - Ownership: `hit` is borrowed for the call.
bool SweepEntity(const SpatialWorld& spatial, SetId set, const Aabbd& box, const Vec3d& motion,
                 const QueryFilter& filter, EntityHit* hit);

/// The `k` entities nearest to a point, nearest first.
///
/// - Param spatial: the world's sets.
/// - Param set: the set to ask.
/// - Param point: the query point.
/// - Param k: how many.
/// - Param max_distance: the farthest to look.
/// - Param filter: the layer mask and key filter.
/// - Param out: receives the entities (cleared first).
/// - Returns: how many were found.
/// - Fails: a set id that names none finds nothing.
/// - Thread-safety: thread-safe against other queries; not against the sync.
/// - Ownership: `out` is borrowed for the call.
usize NearestEntities(const SpatialWorld& spatial, SetId set, const Vec3d& point, u32 k, f64 max_distance,
                      const QueryFilter& filter, Vector<EntityDistance>* out);

namespace detail
{
template <class T> using Plain = RemoveCV<T>;
// A const T is read without stamping; a plain T is written (the stamp of World::Get).
template <class T> T& Fetch(ecs::World& world, ecs::Entity entity)
{
    static_assert(!IsEmpty<Plain<T>>,
                  "ForEachIn: a tag has no data to pass; filter the region's query by the tag another way");
    T* data = nullptr;
    if constexpr (IsConst<T>)
    {
        data = const_cast<T*>(static_cast<const ecs::World&>(world).template Get<Plain<T>>(entity));
    }
    else
    {
        data = world.template Get<T>(entity);
    }
    ENGINE_ASSERT(data != nullptr, "ForEachIn: the component vanished after Has");
    return *data;
}
}  // namespace detail

/// Calls `fn(entity, components...)` for every entity of the set that overlaps `region` and has all the components, in
/// entity id order. A component type written `const T` is read (`const T&`), a plain `T` is written (`T&`, and the
/// chunk is stamped as written like `World::Get`). Entities without one of the components are skipped.
///
/// The hits are collected first and then looked up, so `fn` may write the components but must not change the world's
/// structure (create, destroy, add or remove a component).
///
/// - Param world: the world holding the components.
/// - Param spatial: the world's sets.
/// - Param set: the set to ask.
/// - Param region: the region.
/// - Param filter: the layer mask and key filter.
/// - Param fn: called as `fn(ecs::Entity, Ts&...)`.
/// - Returns: how many entities `fn` was called for.
/// - Aborts: an active ECS query writes one of the components, even when this call only reads it (as for `World::Get`:
/// do not call it from a system that writes that component); a component type is an empty tag (a compile-time error).
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows `world`, `spatial` and `fn` for the call.
template <class... Ts, class Fn>
usize ForEachIn(ecs::World& world, const SpatialWorld& spatial, SetId set, const Aabbd& region,
                const QueryFilter& filter, Fn&& fn)
{
    Vector<ecs::Entity> hits;
    OverlapEntities(spatial, set, region, filter, &hits);
    usize called = 0;
    for (usize i = 0; i < hits.Size(); ++i)
    {
        const ecs::Entity entity = hits[i];
        if (!world.Exists(entity))
        {
            continue;
        }
        bool all = true;
        ((all = all && world.template Has<detail::Plain<Ts>>(entity)), ...);
        if (!all)
        {
            continue;
        }
        fn(entity, detail::Fetch<Ts>(world, entity)...);
        ++called;
    }
    return called;
}

/// One ray's answer in a batch.
///
/// - Thread-safety: thread-compatible plain data.
struct BatchRayHit
{
    /// Whether the ray hit anything.
    bool hit = false;
    /// The nearest item's key (an entity id) when `hit`.
    SpatialKey key = 0;
    /// The parameter of the hit.
    f64 t = 0.0f;
};

/// Casts many rays against a set for their nearest hit, split across the job system when a scheduler is given; `out[i]`
/// answers `rays[i]`, identical to the sequential run.
///
/// - Param spatial: the world's sets.
/// - Param set: the set to ask.
/// - Param rays: the rays.
/// - Param max_t: the longest parameter for every ray.
/// - Param filter: the layer mask and key filter.
/// - Param out: receives one answer per ray; its size must equal `rays.Size()`.
/// - Param scheduler: or null for the calling thread.
/// - Aborts: `out` and `rays` differ in size; with a scheduler, not called from participant 0 outside a job.
/// - Fails: a set id that names none answers "no hit" for every ray.
/// - Thread-safety: owner-thread-only when it uses the scheduler; the set must not change meanwhile.
/// - Ownership: borrows every argument for the call.
void RaycastBatch(const SpatialWorld& spatial, SetId set, Span<const Rayd> rays, f64 max_t, const QueryFilter& filter,
                  Span<BatchRayHit> out, jobs::Scheduler* scheduler);

/// Asks many box overlap questions of a set. The keys of query `i` are `keys[offsets[i]]` up to `keys[offsets[i + 1]]`,
/// each query's keys sorted, the same as the sequential run.
///
/// - Param spatial: the world's sets.
/// - Param set: the set to ask.
/// - Param boxes: the regions.
/// - Param filter: the layer mask and key filter.
/// - Param keys: receives the keys of all queries, concatenated (cleared first).
/// - Param offsets: receives `boxes.Size() + 1` offsets (cleared first).
/// - Param scheduler: or null for the calling thread.
/// - Aborts: with a scheduler, not called from participant 0 outside a job.
/// - Fails: a set id that names none gives every query zero keys.
/// - Thread-safety: owner-thread-only when it uses the scheduler; the set must not change meanwhile.
/// - Ownership: borrows every argument for the call.
void OverlapBatch(const SpatialWorld& spatial, SetId set, Span<const Aabbd> boxes, const QueryFilter& filter,
                  Vector<SpatialKey>* keys, Vector<u32>* offsets, jobs::Scheduler* scheduler);

}  // namespace vse::spatial
