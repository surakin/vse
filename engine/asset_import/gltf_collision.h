#pragma once

//! Collision from a glTF (glTF colliders Days 3 and 4): which nodes are static colliders, and their triangle meshes
//! minted as `MCOL` resources.
//!
//! Two conventions are read. **Node-name suffixes** (case-sensitive, at the end of the node's name after a trailing
//! `.` and digits are set aside, as Blender numbers duplicates): `-col` (the node's mesh is drawn and is a
//! triangle-mesh collider), `-colonly` (a collider only: not drawn), `-convcol` and `-convcolonly` (the same, a convex
//! hull asked for: imported as a triangle mesh, with a warning). And the `OMI_physics_body` / `OMI_physics_shape`
//! extensions (Day 4). Collision is read only when the import makes a level, the one thing that uses it.

#include "asset/resource_id.h"
#include "asset/status.h"
#include "asset_import/gltf_container.h"
#include "asset_import/gltf_error.h"
#include "asset_import/gltf_import.h"
#include "asset_import/gltf_nodes.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "math/vector.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// The collision a node asks for.
enum class GltfCollisionKind : u8
{
    None,     ///< No collider.
    Mesh,     ///< A triangle mesh from the node's (or the shape's) glTF mesh.
    Box,      ///< A box: `half_extents`.
    Sphere,   ///< A sphere: `radius`.
    Capsule,  ///< A capsule along Y: `radius` and `half_height`.
};

/// One node's collision, as read from the file.
///
/// - Thread-safety: thread-compatible plain data.
struct GltfNodeCollision
{
    GltfCollisionKind kind = GltfCollisionKind::None;  ///< What the node asks for.
    bool draw = true;         ///< False when the node's mesh is a collider only (`-colonly`, `-convcolonly`).
    u32 mesh = kNoGltfIndex;  ///< The glTF mesh of a `Mesh` collider.
    Vec3 half_extents = Vec3(0.0f, 0.0f, 0.0f);  ///< A box's half sizes, unscaled.
    f32 radius = 0.0f;                           ///< A sphere's or a capsule's radius, unscaled.
    f32 half_height = 0.0f;                      ///< Half the straight part of a capsule, unscaled.
    f32 friction = 0.5f;                         ///< The surface's friction.
    f32 restitution = 0.0f;                      ///< The surface's bounciness.
};

/// Reads every node's collision mark.
///
/// A marked node without a mesh, or whose mark cannot be honoured, gets `None` and a counted warning
/// (`ImportReport::collision_dropped`).
///
/// - Param document: a parsed glTF.
/// - Param graph: its nodes.
/// - Param report: receives the warnings.
/// - Param out: replaced by one entry per node.
/// - Param error: receives the sentence naming the JSON path.
/// - Fails: `Invalid` for an extension that does not parse (Day 4); never for a name.
/// - Thread-safety: thread-safe for separate calls with separate arguments.
/// - Ownership: borrows its arguments for the call only.
Status ReadCollisionMarks(const GltfDocument& document, const GltfNodeGraph& graph, ImportReport* report,
                          Vector<GltfNodeCollision>* out, GltfError* error);

/// Which glTF meshes only collision-only nodes use: they get no render mesh.
///
/// - Param graph: the nodes.
/// - Param marks: [[asset::ReadCollisionMarks]]'s output.
/// - Param mesh_count: the file's `meshes` count.
/// - Param out: replaced by one flag per glTF mesh.
/// - Fails: never.
void CollisionOnlyMeshes(const GltfNodeGraph& graph, Span<const GltfNodeCollision> marks, usize mesh_count,
                         Vector<bool>* out);

/// Mints one `MCOL` per glTF mesh a `Mesh` collider uses, at `<prefix>/colliders/<mesh name>`.
///
/// The triangles of the mesh's `TRIANGLES` primitives (other modes are skipped with a warning), base positions in the
/// mesh's space with the import's `scale`; a triangle with no area ([[asset::MeshColliderTriangleHasArea]]) is dropped
/// and counted (one warning per mesh); a mesh over the `MCOL` limits, or with no triangle left, gives no collider (a
/// warning; its nodes become `None`). The ids come from the manifest under `ImportKind::MeshCollider`, keyed by the
/// glTF mesh index. Fills `ImportReport`'s collision counts.
///
/// - Param document: a parsed glTF.
/// - Param options: the limits (`max_collision_triangles`), the scale, the ids and the prefix.
/// - Param types: the registry the blobs are minted with.
/// - Param marks: the nodes' collision; a `Mesh` mark whose mesh gives no collider becomes `None`.
/// - Param result: receives the resources, manifest entries, counts and warnings.
/// - Param error: receives the sentence naming the glTF object.
/// - Fails: `Invalid` (a non-finite position, an index outside the vertices, a primitive without `POSITION`),
/// `TooLarge`
///   (above `max_collision_triangles` or `max_output_bytes`), `Cancelled`, `LimitExceeded` (memory).
/// - Thread-safety: thread-safe for separate calls with separate arguments.
/// - Ownership: borrows its arguments for the call only.
Status ImportColliders(const GltfDocument& document, const ImportOptions& options, const TypeRegistry& types,
                       Vector<GltfNodeCollision>* marks, ImportResult* result, GltfError* error);

}  // namespace engine_placeholder::asset
