#pragma once

//! The text source form of a mesh (`.mesh.src`) and the mesh minter. Phase 22 Day 319. An offline tool's code: never
//! linked into the engine; its tests compile it directly.
//!
//! Line-oriented text; `#` starts a comment; fields are separated by spaces or tabs:
//!
//!     id 0123456789abcdef
//!     quality packed                    (or `full`; optional)
//!     skeleton 00000000000000aa         (a skinned mesh names its skeleton)
//!     v px py pz  nx ny nz  tx ty tz tw  u v
//!     i a b c                           (one triangle, vertex numbers from 0)
//!     lod FIRST COUNT SCREEN            (a range of indices, the screen size it is used down to)
//!     slot NAME FIRST COUNT             (a material slot: a range of indices)
//!     skin VERTEX JOINT WEIGHT [JOINT WEIGHT ...]
//!
//! `id` comes first (written by `asset_mint adopt`, Day 333). All `v` records come before the first `i`, `skin` or
//! `lod`/`slot` record that refers to them.

#include "asset/mesh.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/skeleton.h"
#include "asset/status.h"
#include "containers/array.h"
#include "containers/vector.h"
#include "string_view.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// The skin influences a source gave one vertex, before quantization.
///
/// - Thread-safety: thread-compatible (a plain value).
struct MeshSourceSkin
{
    u32 count = 0;               ///< Number of pairs.
    u32 line = 0;                ///< The line of the `skin` record.
    Array<u32, 12> joint = {};   ///< Joint numbers.
    Array<f64, 12> weight = {};  ///< Weights.
};

/// A `morph` record and its `d` records (glTF coverage Day 6).
///
/// - Thread-safety: thread-compatible.
struct MeshSourceMorph
{
    Array<char, kMaxMorphNameBytes + 1> name = {};  ///< The target's name.
    u32 line = 0;                                   ///< The line of the `morph` record.
    Vector<MeshMorphDelta> deltas;                  ///< Its deltas, by vertex.
};

/// A parsed mesh source.
///
/// - Thread-safety: thread-compatible.
struct MeshSource
{
    ResourceId id;                              ///< The resource id from the `id` line.
    MeshQuality quality = MeshQuality::Packed;  ///< The `quality` record.
    ResourceId skeleton;                        ///< The `skeleton` record (none for a static mesh).
    u32 skeleton_line = 0;                      ///< Its line.
    Vector<MeshVertex> vertices;                ///< The `v` records.
    Vector<u32> vertex_lines;                   ///< The line of each `v` record.
    Vector<Array<u8, 4>> colors;                ///< One per vertex when the `v` records carry a colour, else empty.
    Vector<u32> indices;                        ///< The `i` records, three each.
    Vector<MeshLod> lods;                       ///< The `lod` records.
    Vector<MeshSlot> slots;                     ///< The `slot` records.
    Vector<MeshSourceSkin> skin;                ///< One per vertex when any `skin` record exists, otherwise empty.
    Vector<MeshSourceMorph> morphs;             ///< The `morph` records.
};

/// Where and why a mesh source was refused.
///
/// - Thread-safety: immutable value.
struct MeshSourceError
{
    u32 line = 0;                  ///< 1-based line number; 0 when the error is about the file as a whole.
    Array<char, 96> message = {};  ///< A sentence.
};

/// Parses a mesh source.
///
/// - Fails: `Invalid` with `error` filled in: a missing id (the message names `asset_mint adopt`), a repeated id, an
///   unknown record, a wrong field count, a malformed or non-finite number, a `v` after an `i`, an index at or past the
///   vertices defined, a `skin` without a `skeleton` record or for an unknown vertex, a negative weight, a joint number
///   above 255, a vertex skinned twice, a vertex with no positive weight in a skinned mesh, a name longer than 31
///   bytes, no vertices or no indices; `LimitExceeded` for more vertices, indices, LODs or slots than the format
///   allows.
/// - Ownership: borrows `text` for the call; `*out` and `*error` are written.
Status ParseMeshSource(StringView text, MeshSource* out, MeshSourceError* error);

/// The mesh minter: source text to blob bytes, deterministic. A skinned mesh needs the skeleton (a loaded
/// [[asset::SkeletonView]] whose id is the one the source names): its joint count bounds the joint numbers and its hash
/// is recorded.
///
/// - Param skeleton: the skeleton of a skinned mesh, null for a static mesh.
/// - Fails: as `ParseMeshSource` (with `error`); `Invalid` when the source names a skeleton and `skeleton` is null or
/// has
///   another id, or a joint number is at or past its joint count (the line of the `skin` record); and as
///   [[asset::BuildMeshBlob]].
/// - Ownership: borrows its arguments for the call; `*out` is replaced.
Status MintMesh(StringView text, const TypeRegistry& types, const SkeletonView* skeleton, Vector<byte>* out,
                MeshSourceError* error, u32 target = 0);

}  // namespace engine_placeholder::asset
