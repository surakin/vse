// The bridge from ECS entities to physics bodies (Phase 25 Day 388): an entity with `RigidBody`, `Collider` and
// `LocalTransform` and no `Parent` gets a body; a static one with a `Parent` is placed by its `WorldTransform` (glTF
// colliders Day 1).
#pragma once

#include "ecs/world.h"
#include "physics/ids.h"
#include "physics/world.h"
#include "types.h"

namespace vse::physics
{

/// Added to an entity by `SyncBodies`: the body that belongs to it.
///
/// - Thread-safety: thread-compatible plain data.
struct BodyLink
{
    /// The body in the physics world.
    BodyId id;
};

/// What one `SyncBodies` call did.
///
/// - Thread-safety: thread-compatible plain data.
struct SyncReport
{
    /// Bodies made for entities that had none.
    u32 created = 0;
    /// Bodies whose settings or collider changed to follow the entity's components.
    u32 updated = 0;
    /// Bodies destroyed because their entity died or lost a component.
    u32 destroyed = 0;
    /// Entities whose components were refused (not valid): they get no body, and the call reports them again next time.
    u32 refused = 0;
    /// Static bodies with a parent moved to follow their entity's world transform.
    u32 replaced = 0;
};

/// Makes the physics world follow the ECS world.
///
/// It destroys the bodies of entities that are gone or lost their `RigidBody` or `Collider`, updates a body whose
/// components changed, and creates a body for every new entity.
///
/// Entities are visited in entity order (index, then generation), not in chunk order, so two worlds built the same way
/// get the same body ids. A new body starts at the entity's `LocalTransform` and `Velocity` (optional). An entity with
/// a `Parent` is not simulated, except a **static** body: it is placed at the translation and
/// rotation of its `ecs::WorldTransform` (the scale divided out of each column: the caller sizes the shape) and follows
/// it, every call moving the body where the world matrix changed (`PhysicsWorld::SetTransform`, which wakes the bodies
/// touching it, and the movable bodies touching it at the new place are woken too; counted in `replaced`). A world
/// matrix with a column of zero length gives no body (counted as refused). The world matrix is the one the transform
/// propagation last wrote: run it before this call. The call changes the ECS world's structure (it adds and removes
/// `BodyLink`), so it runs between iterations, at a sync point.
///
/// - Param ecs: the ECS world.
/// - Param physics: the physics world.
/// - Returns: what was done.
/// - Aborts: called while the ECS world is being iterated.
/// - Fails: an entity with invalid components is counted in `refused` and skipped.
SyncReport SyncBodies(ecs::World& ecs, PhysicsWorld& physics);

}  // namespace vse::physics
