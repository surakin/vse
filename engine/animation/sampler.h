#pragma once

//! The clip sampler: a clip, a skeleton and a time give a local pose; plus the time advance, the events crossed and the
//! root motion over a step. Nothing is decoded into a table: keys are decoded where they are read.

#include "animation/pose.h"
#include "asset/clip.h"
#include "asset/skeleton.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "types.h"

namespace vse::animation
{

/// The cursor of every channel of every track of the clip being played (the key segment found by the last sample),
/// three per track. Steady forward playback then costs O(1) per channel; a seek or a loop finds its segment by
/// bisection on the next call. Any value is valid: a cursor is a hint, never a result. Sized by
/// [[animation::SampleClip]].
///
/// - Thread-safety: thread-compatible; one state per player.
struct SamplerState
{
    Vector<u32> cursor;  ///< Translation, rotation and scale cursors of each track, in track order.
};

/// The root joint's movement over a step, in the parent space of the root (joint 0): the translation difference and
/// the rotation `to * inverse(from)`.
///
/// - Thread-safety: thread-compatible plain data.
struct RootMotionDelta
{
    Array<f32, 3> translation = {{0.0f, 0.0f, 0.0f}};     ///< Translation difference.
    Array<f32, 4> rotation = {{0.0f, 0.0f, 0.0f, 1.0f}};  ///< Rotation difference (x, y, z, w).
};

/// The result of advancing a play time.
///
/// - Thread-safety: thread-compatible plain data.
struct TimeAdvance
{
    f32 time = 0;           ///< The new time, in [0, duration].
    u32 wraps = 0;          ///< How many times a looping clip passed its end (0 when not looping).
    bool finished = false;  ///< A clip that does not loop reached its end.
};

/// Advances `time` by `delta` seconds (a negative `delta` counts as 0). A looping clip wraps, others stop at the
/// duration.
///
/// - Fails: never.
TimeAdvance AdvanceTime(const asset::ClipView& clip, f32 time, f32 delta, bool looping);

/// Samples `clip` at `time` into `out` (a time past the ends is clamped, or wrapped when `looping`). Joints without a
/// track keep the skeleton's bind pose, or the identity when the clip is additive. A clip flagged root motion holds
/// joint 0 at its first-frame value: its movement is read with [[animation::RootMotionOver]].
///
/// - Param state: the cursors; may be null (every call then bisects).
/// - Aborts: the clip's joint count differs from the skeleton's.
/// - Ownership: borrows its arguments for the call.
void SampleClip(const asset::ClipView& clip, const asset::SkeletonView& skeleton, f32 time, bool looping,
                SamplerState* state, LocalPose* out);

/// The root joint's movement from time `from` to `from + delta` (a looping clip may wrap).
///
/// - Param from: in [0, duration].
/// - Aborts: as `SampleClip`.
RootMotionDelta RootMotionOver(const asset::ClipView& clip, const asset::SkeletonView& skeleton, f32 from, f32 delta,
                               bool looping);

/// Collects the events crossed while time moved from `from` by the advance `advance`, in time order: an event at time
/// `e` is crossed when `from < e <= new time` (a loop adds `from < e <= duration`, any whole passes, and
/// `0 <= e <= new time`). `include_start` also includes an event at exactly `from` (the first frame of a play).
///
/// - Param out: receives up to `out.Size()` events; the rest are dropped and `*overflow` is set.
/// - Param overflow: may be null.
/// - Returns: the number of events written.
/// - Ownership: borrows its arguments for the call.
u32 CollectEvents(const asset::ClipView& clip, f32 from, const TimeAdvance& advance, bool include_start,
                  Span<asset::ClipEvent> out, bool* overflow);

}  // namespace vse::animation
