#pragma once

//! The text source form of an instance set (`.inst.src`) and the instance minter. An offline tool's code: never linked
//! into the engine; its tests compile it directly.
//!
//! Line-oriented text; `#` starts a comment; fields are separated by spaces or tabs:
//!
//!     id 0123456789abcdef
//!     cell_size 4                       (optional: the Morton cell in metres, default 4)
//!     move_threshold 8                  (optional: default 8)
//!     instance X Y Z QX QY QZ QW SX SY SZ [R G B A CUSTOM]
//!
//! An `instance` line is a position in metres (read as a double), a unit rotation and a scale on each axis (above 0),
//! and optionally a tint (four numbers) and a custom value (an unsigned integer); either every instance has the data or
//! none does. `id` comes first (written by `asset_mint adopt`). The instances are added in file order and the set is
//! rebuilt, so the blob is a function of the file.

#include "asset/instance_blob.h"
#include "asset/resource_id.h"
#include "asset/status.h"
#include "containers/array.h"
#include "containers/vector.h"
#include "string_view.h"
#include "types.h"

namespace vse::asset
{

/// A parsed instance source.
///
/// - Thread-safety: thread-compatible.
struct InstanceSource
{
    ResourceId id;                                   ///< From the `id` line.
    renderer::InstanceSetDesc desc;                  ///< `has_data`, the cell size and the move threshold.
    Vector<renderer::InstanceTransform> transforms;  ///< One per `instance` line.
    Vector<renderer::InstanceExtra> data;            ///< One per `instance` line when `desc.has_data`.
};

/// Where and why an instance source was refused.
///
/// - Thread-safety: immutable value.
struct InstanceSourceError
{
    u32 line = 0;                  ///< 1-based line number; 0 when the error is about the file as a whole.
    Array<char, 96> message = {};  ///< A sentence.
};

/// Parses an instance source.
///
/// - Fails: `Invalid` with `error` filled in: a missing id (the message names `asset_mint adopt`), a repeated option,
/// an
///   unknown record, a wrong field count, a number that does not parse or is not finite, a scale not above 0, a
///   rotation that is not a unit quaternion (within 1e-3 of length 1), a position beyond the set's limit, instances
///   with data mixed with instances without, no instance at all, more instances than a set holds.
/// - Ownership: borrows `text` for the call; `*out` and `*error` are written.
Status ParseInstanceSource(StringView text, InstanceSource* out, InstanceSourceError* error);

/// Mints an instance set: parses the source and builds the blob.
///
/// - Fails: as [[asset::ParseInstanceSource]], plus the errors of [[asset::BuildInstanceBlob]] as `Invalid`.
/// - Ownership: borrows its arguments for the call; `*out` is replaced.
Status MintInstances(StringView text, const TypeRegistry& types, Vector<byte>* out, InstanceSourceError* error,
                     u32 target = 0);

}  // namespace vse::asset
