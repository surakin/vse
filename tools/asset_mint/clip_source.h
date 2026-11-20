#pragma once

//! The text source form of an animation clip (`.anim.src`) and the clip minter. Phase 22 Day 320. An offline tool's
//! code: never linked into the engine; its tests compile it directly.
//!
//! Line-oriented text; `#` starts a comment; fields are separated by spaces or tabs:
//!
//!     id 0123456789abcdef
//!     skeleton 0123456789abcdef
//!     duration 1.5
//!     rate 30
//!     flags loop additive rootmotion
//!     bounds minx miny minz maxx maxy maxz
//!     track JOINT
//!     t TIME x y z
//!     r TIME x y z w
//!     s TIME x y z
//!     event TIME NAME [PAYLOAD]
//!
//! `id`, `skeleton` and `duration` are required and come before any `track`. A `t`, `r` or `s` line adds a key to the
//! current `track` (times strictly increasing per channel, seconds within [0, duration]). The skeleton's blob is needed
//! to resolve joint names, and its id and hash are recorded in the clip.

#include "asset/clip.h"
#include "asset/resource_id.h"
#include "asset/skeleton.h"
#include "asset/status.h"
#include "containers/array.h"
#include "containers/vector.h"
#include "string_view.h"
#include "types.h"

namespace vse::asset
{

/// A parsed clip source.
///
/// - Thread-safety: thread-compatible.
struct ClipSource
{
    ResourceId id;                           ///< From the `id` line.
    ResourceId skeleton;                     ///< From the `skeleton` line.
    f32 duration = 0;                        ///< Seconds.
    f32 sample_rate = 30;                    ///< From `rate`.
    u32 flags = 0;                           ///< [[asset::ClipFlags]] from `flags`.
    Array<f32, 3> bounds_min = {{0, 0, 0}};  ///< From `bounds`.
    Array<f32, 3> bounds_max = {{0, 0, 0}};  ///< From `bounds`.
    Vector<ClipTrackSource> tracks;          ///< In file order; the joint resolved by name.
    Vector<ClipEvent> events;                ///< In file order; the name hashed with `JointNameHash`.
};

/// Where and why a source was refused.
///
/// - Thread-safety: immutable value.
struct ClipSourceError
{
    u32 line = 0;                  ///< 1-based line number; 0 when the error is about the file as a whole.
    Array<char, 96> message = {};  ///< A sentence.
};

/// Parses a clip source, resolving joint names against `skeleton`.
///
/// - Fails: `Invalid` with `error` filled in: a missing or repeated header line, an unknown record, a wrong field
///   count, a number that is not finite, an unknown flag, a key or event before its track or out of order, an unknown
///   or repeated joint, a rotation that is not unit length; `LimitExceeded` above the format's counts.
/// - Ownership: borrows `text` and `skeleton` for the call; `*out` and `*error` are written.
Status ParseClipSource(StringView text, const SkeletonView& skeleton, ClipSource* out, ClipSourceError* error);

/// The clip minter: source text to blob bytes, deterministic. The skeleton must be the one the source names.
///
/// - Fails: as `ParseClipSource` (with `error`), and as `BuildClipBlob`.
/// - Ownership: borrows its arguments for the call; `*out` is replaced.
Status MintClip(StringView text, const SkeletonView& skeleton, const TypeRegistry& types, const ClipLimits& limits,
                Vector<byte>* out, ClipSourceError* error, u32 target = 0);

}  // namespace vse::asset
