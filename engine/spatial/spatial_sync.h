#pragma once

//! The spatial sync system (Phase 18 Days 244-245): keeps the sets of a [[spatial::SpatialWorld]] true to the entities that carry the components of
//! `spatial/components.h`.

#include "ecs/schedule.h"
#include "spatial/components.h"
#include "spatial/spatial_world.h"
#include "types.h"

namespace engine_placeholder::spatial {

/// The spatial sync system.
///
/// It keeps a [[spatial::SpatialProxy]] on every entity that is in a set:
///
/// - an entity with `SpatialBounds`, `WorldTransform` and `SpatialMembership` (and no proxy) gets an item in every set its membership names, on the static
///   side when it has [[spatial::SpatialStatic]], and a [[spatial::WorldBounds]]; a box that is not finite, or not a box, is refused and counted, and looked at again
///   every frame;
/// - an entity that loses any of the three components, or is destroyed (found through the world's removal log of `SpatialProxy`, not by scanning), leaves
///   its sets;
/// - a changed `SpatialMembership` adds the item to the sets that are new, removes it from those that are gone and re-adds it when the layer changed; adding or
///   removing `SpatialStatic` moves the items between the static and the dynamic side.
///
/// Needs the resource [[spatial::SpatialWorld]] in the world. Register it after the transform propagation and before any system that queries the sets: a query
/// that runs before the sync in a frame sees the previous frame's sets (documented, not an error).
///
/// - Param name: the system's name, copied by the schedule.
/// - Returns: the system description to `Schedule::Add`.
/// - Aborts: the `SpatialWorld` resource is missing.
/// - Thread-safety: thread-compatible; it writes the sets, so no query may run on them meanwhile.
/// - Ownership: the system owns its queries; the sets belong to the `SpatialWorld`.
ecs::SystemDesc MakeSpatialSyncSystem(const char* name);

} // namespace engine_placeholder::spatial
