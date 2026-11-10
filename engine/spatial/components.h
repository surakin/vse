#pragma once

//! The ECS side of the spatial module (Phase 18 Day 244): the components that put an entity into the spatial sets. The sync system of
//! `spatial/spatial_sync.h` reads them; no other code writes `WorldBounds` or `SpatialProxy`.

#include "containers/array.h"
#include "math/geometry.h"
#include "spatial/spatial_set.h"
#include "types.h"

namespace engine_placeholder::spatial {

/// The entity's box in its own space, normally copied from its mesh. The sync transforms it by the entity's `WorldTransform`.
///
/// Thread-safety: thread-compatible plain data.
struct SpatialBounds {
    /// The local-space box; finite, not empty. A box that is not is refused and counted ([[spatial::SpatialWorldStats::refused_bounds]]).
    Aabb local;
};

/// The entity's box in world space: derived from `SpatialBounds` and `WorldTransform` by the sync, never set by hand. Systems may read it (it is the box the
/// sets hold, without the dynamic tree's margin).
///
/// Thread-safety: thread-compatible plain data.
struct WorldBounds {
    /// The world-space box.
    Aabb box;
};

/// Which sets an entity is in and on which layer. An entity needs `SpatialBounds`, a `WorldTransform` and this to be in a set.
///
/// Thread-safety: thread-compatible plain data.
struct SpatialMembership {
    /// A bit per set id (bit `kRenderSet` is the render set, ...): the entity is in every set whose bit is set. A bit of a set that does not exist is ignored and
    /// counted (a set created later does not pick the entity up until its membership changes) ([[spatial::SpatialWorldStats::unknown_set_bits]]).
    u8 sets = 0;
    /// The layer the entity has in each of its sets (see [[spatial::QueryFilter]]); below 32 (a join with a layer of 32 or more is refused and counted).
    u8 layer = 0;
};

/// A tag: the entity does not move. Its items go into each set's static tree. Adding or removing the tag moves the items between the static and the
/// dynamic side (`SpatialSet::MakeStatic` and `MakeDynamic`).
///
/// Thread-safety: thread-compatible plain data.
struct SpatialStatic {};

/// What the sync keeps on an entity that is in the sets: one handle per set. Owned by the sync system; do not add, change or remove it. The world logs its
/// removal, which is how the sync learns that an entity was destroyed.
///
/// Thread-safety: thread-compatible plain data.
struct SpatialProxy {
    /// The world logs removals of this component (see [[ecs::World::RemovedCount]]).
    static constexpr bool kTrackRemoval = true;
    /// The handle in each set, [[spatial::kInvalidSetHandle]] where the entity is not.
    Array<SetHandle, kMaxSpatialSets> handle;
    /// The sets the entity is in, as of the last sync (a bit per set id).
    u8 sets = 0;
    /// The membership bits the sync last acted on (including bits that named no set), to count a bad bit once.
    u8 requested = 0;
    /// The layer the items have.
    u8 layer = 0;
    /// Whether the items are on the static side.
    bool is_static = false;
    /// Set by the sync when it has taken the entity out of the sets and is about to remove the proxy.
    bool leaving = false;

    /// An entity in no set.
    SpatialProxy() {
        for (usize i = 0; i < kMaxSpatialSets; ++i) {
            handle[i] = kInvalidSetHandle;
        }
    }
};

} // namespace engine_placeholder::spatial
