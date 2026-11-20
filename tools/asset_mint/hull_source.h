#pragma once

//! The text source form of a convex hull (`.hull.src`) and the hull minter. Phase 25 Day 393. An offline tool's code:
//! never linked into the engine; its tests compile it directly.
//!
//! Line-oriented text; `#` starts a comment; fields are separated by spaces or tabs:
//!
//!     id 0123456789abcdef
//!     v x y z
//!
//! `id` comes first (written by `asset_mint adopt`). Every `v` is a point of the cloud; the hull of the cloud is minted
//! (the points inside it are dropped).

#include "asset/hull.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "containers/array.h"
#include "containers/vector.h"
#include "string_view.h"
#include "types.h"

namespace vse::asset
{

/// A parsed hull source.
///
/// - Thread-safety: thread-compatible.
struct HullSource
{
    ResourceId id;                 ///< The resource id from the `id` line.
    Vector<Array<f32, 3>> points;  ///< The points, in file order.
};

/// Where and why a hull source was refused.
///
/// - Thread-safety: immutable value.
struct HullSourceError
{
    u32 line = 0;                  ///< 1-based line number; 0 when the error is about the file as a whole.
    Array<char, 96> message = {};  ///< A sentence.
};

/// Parses a hull source.
///
/// - Fails: `Invalid` with `error` filled in: a missing or repeated `id`, an unknown record, a wrong field count, a
/// number that is malformed or not finite, no points; `LimitExceeded` for the 4097th point.
/// - Ownership: borrows `text` for the call; `*out` and `*error` are written.
Status ParseHullSource(StringView text, HullSource* out, HullSourceError* error);

/// The hull minter: source text to blob bytes, deterministic (the same text gives the same bytes).
///
/// - Fails: as `ParseHullSource` (with `error`), as `BuildHullGeometry` and as `BuildHullBlob` (the message is put in
/// `error`).
/// - Ownership: borrows its arguments for the call; `*out` is replaced.
Status MintHull(StringView text, const TypeRegistry& types, Vector<byte>* out, HullSourceError* error, u32 target = 0);

}  // namespace vse::asset
