#pragma once

//! The text source form of a skeleton (`.skel.src`) and the skeleton minter. An offline tool's code:
//! never linked into the engine; its tests compile it directly.
//!
//! Line-oriented text; `#` starts a comment; fields are separated by spaces or tabs:
//!
//!     id 0123456789abcdef
//!     joint NAME PARENT tx ty tz  qx qy qz qw  sx sy sz
//!     ibm NAME m00 m01 ... m11
//!
//! `id` comes first (written by `asset_mint adopt`). `joint` records come in parent-first order; PARENT is a
//! joint name or `-` for a root. `ibm` (12 numbers, a 3x4 row-major matrix) overrides one joint's computed inverse bind
//! matrix.

#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/skeleton.h"
#include "asset/status.h"
#include "containers/array.h"
#include "containers/vector.h"
#include "string_view.h"
#include "types.h"

namespace vse::asset
{

/// A parsed skeleton source.
///
/// - Thread-safety: thread-compatible.
struct SkeletonSource
{
    ResourceId id;                 ///< The resource id from the `id` line.
    Vector<SkeletonJoint> joints;  ///< The joints, in file order, quaternions normalized, inverse binds not yet set.
    Vector<bool> has_ibm;          ///< Per joint: an `ibm` line gave its inverse bind matrix.
};

/// Where and why a source was refused.
///
/// - Thread-safety: immutable value.
struct SkeletonSourceError
{
    u32 line = 0;                  ///< 1-based line number; 0 when the error is about the file as a whole.
    Array<char, 96> message = {};  ///< A sentence.
};

/// Parses a skeleton source. Quaternions are normalized in double precision and moved to the w >= 0 hemisphere.
///
/// - Fails: `Invalid` with `error` filled in: missing or repeated `id`, unknown record, wrong field count, a number
/// that
///   is not finite, an unknown or later parent, a duplicate name, a name over 64 bytes, a quaternion that is zero, not
///   finite or not within 1e-2 of unit length, a zero scale, an `ibm` for an unknown joint or given twice, no joints;
///   `LimitExceeded` for the 257th joint.
/// - Ownership: borrows `text` for the call; `*out` and `*error` are written.
Status ParseSkeletonSource(StringView text, SkeletonSource* out, SkeletonSourceError* error);

/// The skeleton minter: source text to blob bytes, deterministic (the same text gives the same bytes). Computes the
/// inverse bind matrices that the source does not give, then builds the blob with names.
///
/// - Fails: as `ParseSkeletonSource` (with `error`), and as `BuildSkeletonBlob`.
/// - Ownership: borrows its arguments for the call; `*out` is replaced.
Status MintSkeleton(StringView text, const TypeRegistry& types, Vector<byte>* out, SkeletonSourceError* error,
                    u32 target = 0);

}  // namespace vse::asset
