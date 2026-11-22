#pragma once

//! The text source form of a static mesh collider (`.mcol.src`) and its minter. An offline tool's
//! code: never linked into the engine; its tests compile it directly.
//!
//!     id 0123456789abcdef
//!     v x y z
//!     t a b c [material]
//!
//! `id` comes first. `v` lines are the vertices in order; `t` lines are triangles by vertex index (0-based) with an
//! optional material index (0-255).

#include "asset/mesh_collider.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "containers/array.h"
#include "containers/vector.h"
#include "string_view.h"
#include "types.h"

namespace vse::asset
{

/// A parsed mesh-collider source.
struct McolSource
{
    ResourceId id;
    Vector<Array<f32, 3>> positions;
    Vector<u32> indices;
    Vector<u8> materials;
};

/// Where and why a source was refused.
struct McolSourceError
{
    u32 line = 0;                  ///< 1-based line number; 0 for the file as a whole.
    Array<char, 96> message = {};  ///< A sentence.
};

/// Parses the text.
///
/// - Fails: `Invalid` for a malformed line; `LimitExceeded` over the vertex or triangle limit. `error` says which line.
Status ParseMcolSource(StringView text, McolSource* out, McolSourceError* error);

/// Parses and mints.
///
/// - Fails: the errors of [[asset::ParseMcolSource]] and [[asset::BuildMeshColliderBlob]].
Status MintMeshCollider(StringView text, const TypeRegistry& types, Vector<byte>* out, McolSourceError* error,
                        u32 target = 0);

}  // namespace vse::asset
