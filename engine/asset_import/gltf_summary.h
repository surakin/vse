#pragma once

//! The facts about an imported glTF that the minted blobs do not hold in a form a host can read cheaply: where the
//! named nodes are, how big the scene is, which materials, animations and lights the file has.
//!
//! Maintenance (2026-11-18, the host cache of imported resources). A host that adds the blobs of an import back without
//! importing again (from a disk cache) still needs a few facts about the file: the position of the node named
//! `Spawn`, the bounding box of the scene (the walkable area of a world), the height of an avatar, whether a material
//! named `Screen` and animations named `Walk` and `Idle` exist, and the position of a node named `Eye`. The engine's C
//! API has no query for the bounds of a loaded level, a material by name or the duration of a clip, so the importer
//! writes them once, as UTF-8 JSON, into `ImportResult::summary`; the host stores that text beside the blobs.
#include "asset_import/gltf_import.h"
#include "asset_import/gltf_nodes.h"

namespace engine_placeholder::asset
{

/// The version of the summary's JSON layout (the member `"version"`).
inline constexpr u32 kImportSummaryVersion = 1;

/// Writes the summary of the nodes of a level into `result->summary`.
///
/// The text is one JSON object:
/// `{"version":1,"nodes":[{"name":"Spawn","position":[x,y,z],"mesh":-1|index},...],"bounds":{"min":[x,y,z],"max":[x,y,z]}`
/// `(or null when no mesh node has a POSITION
/// range),"materials":["name",...],"animations":[{"name":"Walk","duration":s},...],`
/// `"lights":[{"name":"Lamp","type":"point"|"spot"|"directional","node":"Lamp","position":[x,y,z]},...]}`. `nodes`
/// lists the entities of the level in the order of the level (names as `level_spawn` gives them, unique; `position` is
/// the node's world position times `options.scale`); `bounds` is the box of the meshes' POSITION ranges (the accessors'
/// `min` and `max`) under the nodes' world matrices, times `options.scale`; `materials` and `animations` are in the
/// file's order (an unnamed one has the name `""`), the `duration` being the longest sampler input range in seconds (0
/// when the accessors give no range); `lights` has one entry per level node that names a `KHR_lights_punctual` light,
/// in level order: the light's own name (`""` if unnamed), its glTF type, the entity it is on (`node`, a name of
/// `nodes`) and its world position times `options.scale`. Numbers are written with 9 significant digits.
///
/// - Param document: a parsed glTF.
/// - Param graph: its node graph.
/// - Param options: `scale`.
/// - Param nodes: the indexes into `graph.nodes` of the level's entities, in level order.
/// - Param names: the entity name of each of `nodes` (as long as `nodes`).
/// - Param result: receives `summary` (replaced).
/// - Fails: never (a range the file does not give is left out of the facts).
/// - Thread-safety: thread-safe for separate calls with separate arguments.
/// - Ownership: borrows its arguments for the call only.
void BuildImportSummary(const GltfDocument& document, const GltfNodeGraph& graph, const ImportOptions& options,
                        const Vector<u32>& nodes, const Vector<String<HeapAllocator>>& names, ImportResult* result);

}  // namespace engine_placeholder::asset
