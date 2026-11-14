#pragma once

//! Importing glTF meshes into mesh blobs.
//!
//! Phase 22 Day 336. Each glTF mesh becomes one engine mesh resource whose material slots are the glTF primitives,
//! built through [[asset::BuildMeshBlob]]: what is imported is exactly what a `.mesh.src` of the same data would mint.
//! Missing attributes are made deterministically (normals, tangents, zero texture coordinates); what the mesh format
//! cannot hold (morph targets, vertex colors, a second texture-coordinate set) is refused or dropped with a counted
//! warning.

#include "asset/content_hash.h"
#include "asset/mesh.h"
#include "asset_import/gltf_container.h"
#include "asset_import/gltf_import.h"
#include "containers/span.h"

namespace engine_placeholder::asset
{

/// The skeleton a glTF mesh is skinned to, resolved by the caller (skeletons are imported from Day 337).
///
/// - Thread-safety: thread-compatible plain data.
struct MeshSkeletonBinding
{
    bool present = false;       ///< The mesh is skinned to a skeleton of this import.
    ResourceId skeleton;        ///< The skeleton resource.
    ContentHash skeleton_hash;  ///< Its content hash.
    u32 joint_count = 0;        ///< Its joint count (1 to 256).
};

/// Imports every mesh of a parsed glTF.
///
/// Appends one mesh resource per glTF mesh to `result->resources` (path `<prefix>/meshes/<name>`), their ids to
/// `result->manifest` (ids of `options.manifest` kept, new ones from `options.ids`) and the warnings to
/// `result->report`. `bindings` holds one entry per glTF mesh (or fewer: the missing ones are not skinned); a primitive
/// with `JOINTS_0` of a mesh without a binding is imported unskinned with a counted warning.
///
/// - Param document: a parsed glTF ([[asset::ParseGltf]]).
/// - Param options: limits, ids, manifest, path prefix and the mesh options.
/// - Param types: the registry the blobs are minted with.
/// - Param bindings: the skeleton of each mesh, by glTF mesh index.
/// - Param result: receives the resources, manifest entries and warnings (appended to).
/// - Param error: receives the sentence naming the glTF mesh and primitive.
/// - Fails: `Invalid` (a mesh without primitives, a primitive without `POSITION` or triangles, mismatched attribute
///   counts, an attribute of the wrong type, an index past the vertices, a strip with fewer than three indices, a skin
///   joint past the skeleton), `Unsupported` (a primitive mode that is not triangles, morph targets without
///   `drop_morph_targets`), `TooLarge` (above a limit), `LimitExceeded` (memory).
/// - Thread-safety: thread-safe for separate calls with separate arguments.
/// - Ownership: borrows its arguments for the call only; `*result` owns what it receives.
Status ImportMeshes(const GltfDocument& document, const ImportOptions& options, const TypeRegistry& types,
                    Span<const MeshSkeletonBinding> bindings, ImportResult* result, GltfError* error);

/// Cleans a glTF name into one resource-path segment: `/`, `\`, control characters become `_`, `.` and `..` become `_`.
///
/// - Param name: the name as written.
/// - Param out: replaced by the segment (empty for an empty name).
/// - Ownership: borrows its arguments for the call only; nothing is retained.
/// - Fails: never.
void CleanPathSegment(StringView name, String<HeapAllocator>* out);

/// Appends a warning to a report: counts it and keeps its sentence (the first 256 are kept).
///
/// - Param report: the report.
/// - Param where: the glTF object.
/// - Param what: what was dropped or approximated.
/// - Ownership: borrows its arguments for the call only; nothing is retained.
/// - Fails: never.
void AddImportWarning(ImportReport* report, StringView where, StringView what);

}  // namespace engine_placeholder::asset
