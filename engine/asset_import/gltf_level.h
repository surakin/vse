#pragma once

//! The glTF scene as a level: one entity per node of the default scene.
//!
//! Phase 24 Day 364 (the scene output moved out of Phase 22 Day 338). Every node reachable from the default scene's
//! root nodes becomes an entity of a level blob: its name, its local transform (translation, rotation, scale), its
//! parent and, for a node with a mesh, the imported mesh and the material of the mesh's first primitive, and for a node
//! with a skin the skeleton instance. Several nodes that use one glTF mesh give several entities that share one mesh
//! resource. The level is a resource like the others (path `<prefix>/levels/<scene>`), added with the rest by
//! `ImportIntoBackend`.
#include "asset_import/gltf_skin.h"

namespace engine_placeholder::asset
{

/// The deepest node hierarchy a level holds, a root being depth 0: one below the ECS's own bound
/// (`ecs::kMaxHierarchyDepth`, 64), because `level_spawn` puts a root entity of its own above the level's roots.
inline constexpr u32 kMaxLevelDepth = 63;

/// Makes the level of the default scene of a parsed glTF from the resources already imported.
///
/// Appends the level to `result->resources` and its id to `result->manifest` (`ImportKind::Level`). A file with no
/// scene, or a default scene with no nodes, makes no level and a note in the report. A mesh node whose mesh was not
/// imported has no mesh reference (a warning).
///
/// - Param document: a parsed glTF.
/// - Param graph: its node graph.
/// - Param options: limits, ids, manifest, path prefix, `scale`.
/// - Param types: the registry the blob is minted with.
/// - Param skins: the skeletons imported from the file's skins.
/// - Param result: holds the meshes, materials and skeletons already imported; the level is appended to it.
/// - Param error: receives the sentence naming the node or the scene.
/// - Fails: `Invalid` (a scene root that has a parent, a scene index outside `scenes`, a node transform with a shear),
///   `TooLarge` (a hierarchy deeper than [[asset::kMaxLevelDepth]], more entities than a level holds, the output
///   limit), `LimitExceeded` (memory).
/// - Thread-safety: thread-safe for separate calls with separate arguments.
/// - Ownership: borrows its arguments for the call only; `*result` owns what it receives.
Status ImportLevel(const GltfDocument& document, const GltfNodeGraph& graph, const ImportOptions& options,
                   const TypeRegistry& types, Span<const ImportedSkin> skins, ImportResult* result, GltfError* error);

}  // namespace engine_placeholder::asset
