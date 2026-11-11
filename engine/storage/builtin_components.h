#pragma once

//! The engine's own components, registered for storage (Phase 19 Day 256): the transform and hierarchy of `ecs` and the membership and bounds of `spatial`.
//! Ids below 1000 belong to this file; a game registers its own components from 1000 up.

#include "storage/component_registry.h"

namespace engine_placeholder::storage {

/// The stable ids of the engine's components. Never change or reuse one.
enum BuiltinComponentId : u32 {
    kLocalTransformId = 1,      ///< `ecs::LocalTransform`: stored.
    kWorldTransformId = 2,      ///< `ecs::WorldTransform`: derived by the propagation system.
    kParentId = 3,              ///< `ecs::Parent`: skipped; its stored form is `ParentLink` (`CaptureParents` before a save, `ApplyParents` after a load).
    kHierarchyDepthId = 4,      ///< `ecs::HierarchyDepth`: derived by `ecs::SetParent`.
    kEntityKeyId = 5,           ///< `storage::EntityKeyComponent`: the durable identity of an entity.
    kParentLinkId = 6,          ///< `storage::ParentLink`: the stored form of `ecs::Parent`, a key.
    kOpaqueComponentsId = 7,    ///< `storage::OpaqueComponents`: records of unknown components kept by `KeepOpaque`; saved back unchanged.
    kSpatialBoundsId = 10,      ///< `spatial::SpatialBounds`: stored.
    kSpatialMembershipId = 11,  ///< `spatial::SpatialMembership`: stored.
    kSpatialStaticId = 12,      ///< `spatial::SpatialStatic`: a stored tag.
    kWorldBoundsId = 13,        ///< `spatial::WorldBounds`: derived by the spatial sync.
    kSpatialProxyId = 14,       ///< `spatial::SpatialProxy`: derived by the spatial sync.
};

/// Registers the engine's components in `registry`: the stored ones with their fields, the derived ones flagged `kComponentDerived`.
///
/// Param registry: the registry to fill; a registration that exists already is reported, not replaced.
/// Ownership: `registry` is borrowed for the call.
/// Fails: `Exists` when any of these ids, names or types is registered already (the registry then holds the ones before it).
Status RegisterBuiltinComponents(ComponentRegistry* registry);

} // namespace engine_placeholder::storage
