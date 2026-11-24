#pragma once

//! The text source form of an instance tree (`.itree.src`) and its minter. An offline tool's code: never linked into
//! the engine; its tests compile it directly.
//!
//! Line-oriented text; `#` starts a comment; fields are separated by spaces or tabs:
//!
//!     id 0123456789abcdef
//!     mesh MINX MINY MINZ MAXX MAXY MAXZ                         (one per mesh, in index order; at most 256)
//!     node PARENT X Y Z QX QY QZ QW SX SY SZ MESH SLOT [R G B A CUSTOM]
//!
//! A node's PARENT is `-` for a root or the index (from 0, in the order of the `node` lines) of an earlier node; X Y Z
//! is a root's world position or the offset from the parent; MESH is a mesh index or `-` for a node that draws nothing;
//! SLOT is a material slot. Either every node has the tint and custom value or none does.

#include "asset/instance_tree_blob.h"
#include "asset/resource_id.h"
#include "asset/status.h"
#include "containers/array.h"
#include "containers/vector.h"
#include "string_view.h"
#include "types.h"

namespace vse::asset
{

/// A parsed instance tree source.
///
/// - Thread-safety: thread-compatible.
struct InstanceTreeSource
{
    ResourceId id;                          ///< From the `id` line.
    Vector<renderer::TreeMeshInfo> meshes;  ///< One per `mesh` line.
    Vector<InstanceTreeSourceNode> nodes;   ///< One per `node` line.
};

/// Where and why an instance tree source was refused.
///
/// - Thread-safety: immutable value.
struct InstanceTreeSourceError
{
    u32 line = 0;                  ///< 1-based line number; 0 when the error is about the file as a whole.
    Array<char, 96> message = {};  ///< A sentence.
};

/// Parses an instance tree source.
///
/// - Fails: `Invalid` with `error` filled in: a missing id, an unknown record, a wrong field count, a number that does
///   not parse, a parent that is not an earlier node, a mesh or slot out of range, a scale not above 0, a rotation that
///   is not a unit quaternion (within 1e-3 of length 1), a tree deeper than the limit, no node at all, data on some
///   nodes only.
/// - Ownership: borrows `text` for the call; `*out` and `*error` are written.
Status ParseInstanceTreeSource(StringView text, InstanceTreeSource* out, InstanceTreeSourceError* error);

/// Mints an instance tree: parses the source and builds the blob.
///
/// - Fails: as [[asset::ParseInstanceTreeSource]], plus the errors of [[asset::BuildInstanceTreeBlob]] as `Invalid`.
/// - Ownership: borrows its arguments for the call; `*out` is replaced.
Status MintInstanceTree(StringView text, const TypeRegistry& types, Vector<byte>* out, InstanceTreeSourceError* error,
                        u32 target = 0);

}  // namespace vse::asset
