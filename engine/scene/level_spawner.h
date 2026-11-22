#pragma once

//! Spawning a level into a world through the resource manager.
//!
//! [[scene::SpawnLevel]] loads a level and its closure (meshes, materials, textures, skeletons, clips, included
//! levels), creates its entities with [[asset::LoadLevelBlob]], and gives each [[asset::MeshReference]] a
//! [[scene::MeshRenderer]] that shares the manager's mesh, so the render sync draws it. The entities and the
//! [[scene::SpawnedLevel]] go together: while the spawned level lives, what its entities use stays loaded.

#include "asset/cpu_loaders.h"
#include "asset/gpu_loaders.h"
#include "asset/level.h"
#include "asset/resource_manager.h"
#include "ecs/world.h"
#include "scene/components.h"
#include "storage/component_registry.h"
#include "storage/identity.h"

namespace vse::scene
{

/// The references a spawned level holds: its levels (itself and the levels it includes), so their closures stay loaded,
/// the materials its entities draw with, and the keys of the entities it made.
///
/// - Thread-safety: thread-compatible; destroy it on a thread that may touch the manager.
/// - Ownership: owns the references; the manager must outlive it. Destroying it does not destroy the entities: use
///   [[scene::DestroyLevel]] first.
struct SpawnedLevel
{
    Vector<asset::Handle<asset::LevelSummary>> levels;  ///< The level and the ones it includes.
    Vector<asset::GpuMaterialPtr>
        materials;  ///< The materials its entities draw with (shared: a reload of the resource leaves them).
    Vector<storage::EntityKey> entities;  ///< The entities it made, in the order they were created.
};

/// Loads the level `id` through `manager` and creates its entities (and those of the levels it includes, first) in
/// `world`; every `mesh_reference` component gets a [[scene::MeshRenderer]] whose mesh is the manager's and whose
/// material is the material system's slot of the referenced material; an entity with a local transform also gets the
/// world transform the transform system fills (a level does not store derived state). The levels' dependencies must
/// load as the GPU types ([[asset::RegisterGpuLoaders]]).
///
/// - Param manager: the manager with the CPU and GPU loaders registered.
/// - Param id: the level.
/// - Param registry: the components (builtin, level and the game's).
/// - Param world: receives the entities; it must hold none of this level's keys.
/// - Param index: the keys of `world`; updated.
/// - Param out: receives the references; replaced.
/// - Fails: the failures of [[asset::ResourceManager::Load]] for the level or anything it needs (`MissingDependency`,
///   `DependencyTypeMismatch`, ...), and of [[asset::LoadLevelBlob]] (`Exists` for a key already in the world);
///   `LimitExceeded` for includes nested deeper than 16 levels; on any failure the entities made so far are destroyed
///   and `*out` holds nothing.
/// - Thread-safety: thread-compatible; the thread that owns the world.
/// - Ownership: borrows its arguments for the call; `*out` owns references to loaded resources.
asset::Status SpawnLevel(asset::ResourceManager& manager, asset::ResourceId id,
                         const storage::ComponentRegistry& registry, ecs::World& world, storage::KeyIndex& index,
                         SpawnedLevel* out);

/// Destroys the entities a level made and releases what it held.
///
/// - Param level: what [[scene::SpawnLevel]] returned; emptied.
/// - Param world: the world the level was spawned into.
/// - Param index: its keys.
/// - Ownership: borrows its arguments for the call.
void DestroyLevel(SpawnedLevel* level, ecs::World& world, storage::KeyIndex& index);

}  // namespace vse::scene
