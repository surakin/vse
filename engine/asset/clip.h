#pragma once

//! The animation clip blob (type `ANIM`, version 1): per-joint keyframe tracks and events for one skeleton, built by
//! [[asset::BuildClipBlob]] and read in place through [[asset::ClipView]]. Phase 22 Day 320.
//!
//! Sections (little-endian, ids from 16): 16 header (96 bytes: duration, sample rate hint, flags, joint count, track
//! count, event count, the skeleton hash, the measured error bounds, the bound box), 17 the track table (72 bytes per
//! animated joint), 18 the key data, 19 the events (16 bytes each). The skeleton is the blob's one dependency (role
//! [[asset::kClipRoleSkeleton]]).
//!
//! A channel (translation, rotation or scale) of a track is either constant (its value sits in the track table, no
//! keys) or keyed. Keyed times are `u16` ticks: 1 / sample rate when every key lies on that grid, otherwise duration /
//! 65535. Rotations are smallest-three: the largest component is dropped (made positive), the other three stored as
//! `SNorm16`, the first of them as 14 bits with the dropped index in the low two bits of its word (6 bytes per key).
//! Translations and scales are `UNorm16x3` between a per-channel minimum and extent stored with the channel. Sampling
//! interpolates linearly (rotations: normalized shortest-arc lerp).

#include "asset/blob.h"
#include "asset/content_hash.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/skeleton.h"
#include "asset/status.h"
#include "containers/array.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// Most animated joints of one clip.
inline constexpr u32 kMaxClipTracks = 256;
/// Most keys of one channel.
inline constexpr u32 kMaxClipKeys = 65535;
/// Most events of one clip.
inline constexpr u32 kMaxClipEvents = 1024;
/// The dependency role of a clip's skeleton.
inline constexpr u32 kClipRoleSkeleton = 1;

/// Clip flags.
enum ClipFlags : u32
{
    kClipLooping = 1,     ///< The clip loops.
    kClipAdditive = 2,    ///< The clip is a difference to add onto another pose.
    kClipRootMotion = 4,  ///< The root joint's track carries motion the game applies to the entity.
};

/// The section ids of a clip blob.
enum ClipSection : u32
{
    kClipSectionHeader = 16,   ///< The header, 96 bytes.
    kClipSectionTracks = 17,   ///< The track table, 72 bytes per track.
    kClipSectionKeys = 18,     ///< Key times and values.
    kClipSectionEvents = 19,   ///< The events, 16 bytes each.
    kClipSectionWeights = 20,  ///< The weight track (glTF coverage Day 9): 16 bytes of counts, the key times, the
                               ///< values.
};

/// A translation or scale key.
///
/// - Thread-safety: immutable value.
struct ClipKey3
{
    f32 time = 0;                       ///< Seconds from the start.
    Array<f32, 3> value = {{0, 0, 0}};  ///< The value.
};

/// A rotation key.
///
/// - Thread-safety: immutable value.
struct ClipKey4
{
    f32 time = 0;                          ///< Seconds from the start.
    Array<f32, 4> value = {{0, 0, 0, 1}};  ///< A quaternion (x, y, z, w), unit length.
};

/// The keys of one joint, as the minter takes them (times strictly increasing and within the duration).
///
/// - Thread-safety: thread-compatible.
struct ClipTrackSource
{
    u32 joint = 0;                 ///< The joint index.
    Vector<ClipKey3> translation;  ///< May be empty (the joint is not animated in translation).
    Vector<ClipKey4> rotation;     ///< May be empty.
    Vector<ClipKey3> scale;        ///< May be empty.
};

/// One event: a time, the hash of a name and a payload.
///
/// - Thread-safety: immutable value.
struct ClipEvent
{
    f32 time = 0;       ///< Seconds from the start.
    u32 name_hash = 0;  ///< [[asset::JointNameHash]] of the event name.
    u32 payload = 0;    ///< Free for the game.
};

/// The morph weights a clip animates (glTF coverage Day 9), as the minter takes them.
///
/// They are the weights of the entity that plays the clip, keyed linearly, and stored as they are (`f32`), not reduced
/// or quantized.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows the spans.
struct ClipWeightTrack
{
    u32 target_count = 0;    ///< Weights per key, at most 64; 0 for a clip without weights.
    Span<const f32> times;   ///< Strictly increasing, within [0, duration]; at least one.
    Span<const f32> values;  ///< `times.Size() * target_count`, key by key.
};

/// The error bounds a minted clip is held to.
///
/// - Thread-safety: immutable value.
struct ClipLimits
{
    f32 rotation_degrees = 0.05f;  ///< Largest rotation error in degrees (setting
                                   ///< `assets.anim_rotation_error_degrees`).
    f32 translation = 1e-4f;       ///< Largest translation or scale error relative to a channel's extent.
};

/// What [[asset::BuildClipBlob]] takes.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows the spans for the call.
struct ClipBuildInput
{
    f32 duration = 0;                        ///< Seconds, above 0.
    f32 sample_rate = 30;                    ///< A hint for tools (the rate the source was authored at).
    u32 flags = 0;                           ///< [[asset::ClipFlags]].
    ResourceId skeleton;                     ///< The skeleton the clip animates.
    ContentHash skeleton_hash;               ///< Its hash.
    u32 joint_count = 0;                     ///< Its joint count.
    Array<f32, 3> bounds_min = {{0, 0, 0}};  ///< The clip's box (bind-pose bound until the evaluator exists).
    Array<f32, 3> bounds_max = {{0, 0, 0}};  ///< See `bounds_min`.
    Span<const ClipTrackSource> tracks;      ///< At most one per joint.
    Span<const ClipEvent> events;            ///< Times non-decreasing.
    ClipWeightTrack weights;                 ///< The weight track, or none (`target_count` 0).
    ClipLimits limits;                       ///< The error bounds the clip must meet.
};

/// One joint's sampled local pose.
///
/// - Thread-safety: immutable value.
struct ClipSample
{
    Array<f32, 3> translation = {{0, 0, 0}};  ///< Local translation.
    Array<f32, 4> rotation = {{0, 0, 0, 1}};  ///< Local rotation (unit).
    Array<f32, 3> scale = {{1, 1, 1}};        ///< Local scale.
    bool has_translation = false;             ///< The track animates translation (otherwise the bind pose applies).
    bool has_rotation = false;                ///< The track animates rotation.
    bool has_scale = false;                   ///< The track animates scale.
};

/// Builds a clip blob: reduces the keys of every channel (a deterministic greedy split that keeps the error inside the
/// bounds), folds constant channels into the track table, quantizes, measures the error against the source keys at
/// every source key and at 8 points inside each segment, and records the measured bounds in the header.
///
/// A clip with a weight track and no joint tracks may have no skeleton (a null id and a joint count of 0); it then has
/// no dependency and binds to no skeleton.
///
/// - Fails: `Invalid` (no tracks, no events and no weights, a duration that is not finite and above 0, a weight track
///   whose times or values are out of order, outside the duration, not finite or of the wrong count, a joint at or past
///   the joint count or used twice, keys not strictly increasing or outside [0, duration], a value that is not finite,
///   a rotation that is not unit length, events out of order or past the duration, a null skeleton); `LimitExceeded` (a
///   count above the limits, or a measured error above `limits`).
/// - Ownership: borrows `input` for the call; `*out` is replaced; `*rotation_degrees` and `*translation`, when not
/// null,
///   receive the measured errors (also on a `LimitExceeded` failure).
Status BuildClipBlob(const TypeRegistry& types, ResourceId id, const ClipBuildInput& input, Vector<byte>* out,
                     u32 target = 0, f32* rotation_degrees = nullptr, f32* translation = nullptr);

/// A clip blob read in place.
///
/// - Thread-safety: thread-compatible; immutable once opened.
/// - Ownership: borrows the blob bytes, which must outlive the view.
class ClipView
{
public:
    /// Creates an empty view.
    ClipView() = default;

    /// Validates `data` as a clip blob of the current version and keeps spans into it.
    ///
    /// - Fails: the errors of [[asset::BlobReader::Open]]; `TypeMismatch` for another type; `Corrupt` for a missing or
    ///   mis-sized section, a count above the limit, a track whose joint is out of range or not above the previous
    ///   one, a channel that is both constant and keyed or keyed with fewer than 2 keys, a key block that is outside
    ///   the key section or misaligned, key times that do not increase, a constant that is not finite (or a rotation
    ///   that is not unit length), events out of order or past the duration, a non-finite header value, or a missing or
    ///   wrong skeleton dependency.
    /// - Ownership: borrows `data` and `types` for the call; the view keeps spans into `data`.
    Status Open(ByteSpan data, const TypeRegistry& types);

    /// The duration in seconds.
    f32 Duration() const { return duration_; }
    /// The sample rate hint.
    f32 SampleRate() const { return sample_rate_; }
    /// The flags.
    u32 Flags() const { return flags_; }
    /// True when the clip loops.
    ///
    /// - Fails: never.
    bool Looping() const { return (flags_ & kClipLooping) != 0; }
    /// The number of morph weights the clip's weight track animates (0 without one; glTF coverage Day 9).
    u32 WeightTargetCount() const { return weight_targets_; }
    /// The number of keys of the weight track.
    u32 WeightKeyCount() const { return weight_keys_; }
    /// Samples the weight track at `time` (clamped to its first and last key), linearly between keys.
    ///
    /// - Param time: seconds from the start.
    /// - Param out: receives the weights.
    /// - Param count: the room at `out`; the first `min(count, WeightTargetCount())` are written.
    /// - Ownership: `out` is borrowed for the call.
    void SampleWeights(f32 time, f32* out, u32 count) const;
    /// The joint count of the skeleton the clip was made for (0 for a clip that animates weights only).
    u32 JointCount() const { return joint_count_; }
    /// The number of animated joints.
    u32 TrackCount() const { return track_count_; }
    /// The skeleton the clip animates.
    ResourceId Skeleton() const { return skeleton_; }
    /// The recorded skeleton hash.
    ContentHash SkeletonHash() const { return skeleton_hash_; }
    /// The measured rotation error of the clip, in degrees.
    f32 RotationError() const { return rotation_error_; }
    /// The measured translation and scale error, relative to a channel's extent.
    f32 TranslationError() const { return translation_error_; }
    /// The clip's box.
    Array<f32, 3> BoundsMin() const { return bounds_min_; }
    /// The clip's box.
    Array<f32, 3> BoundsMax() const { return bounds_max_; }
    /// The number of events.
    u32 EventCount() const { return event_count_; }
    /// Event `i`.
    ///
    /// - Aborts: `i` is not below `EventCount()`.
    ClipEvent Event(u32 i) const;
    /// The joint animated by track `i`.
    ///
    /// - Aborts: `i` is not below `TrackCount()`.
    u32 TrackJoint(u32 i) const;
    /// The key count of channel `channel` (0 translation, 1 rotation, 2 scale) of track `i`; 0 when the channel is
    /// constant or absent.
    ///
    /// - Aborts: `i` is not below `TrackCount()` or `channel` is above 2.
    u32 KeyCount(u32 i, u32 channel) const;
    /// True when channel `channel` of track `i` is constant (its value sits in the track table, no keys).
    ///
    /// - Aborts: as `KeyCount`.
    bool IsConstant(u32 i, u32 channel) const;

    /// Samples track `i` at `time` (clamped to [0, duration]; a NaN time reads as 0).
    ///
    /// `cursors`, when given, points at 3 `u32` (translation, rotation, scale) that remember the key segment found by
    /// the last call: forward playback then costs O(1) per channel, and the result is the same as without them (any
    /// start value is valid; keep one set per track and clip).
    ///
    /// - Aborts: `i` is not below `TrackCount()`.
    /// - Ownership: borrows its arguments for the call.
    ClipSample Sample(u32 i, f32 time, u32* cursors = nullptr) const;

    /// Checks that `skeleton` is the one the clip was made for (id, hash and joint count).
    ///
    /// - Fails: `Invalid` naming the difference.
    Status Bind(const SkeletonView& skeleton) const;

private:
    f32 duration_ = 0;
    f32 sample_rate_ = 0;
    f32 ticks_per_second_ = 0;
    u32 flags_ = 0;
    u32 joint_count_ = 0;
    u32 track_count_ = 0;
    u32 event_count_ = 0;
    f32 rotation_error_ = 0;
    f32 translation_error_ = 0;
    Array<f32, 3> bounds_min_ = {{0, 0, 0}};
    Array<f32, 3> bounds_max_ = {{0, 0, 0}};
    ResourceId skeleton_;
    ContentHash skeleton_hash_;
    ByteSpan tracks_;
    ByteSpan keys_;
    ByteSpan events_;
    ByteSpan weights_;  // the weight section's times and values
    u32 weight_targets_ = 0;
    u32 weight_keys_ = 0;
};

}  // namespace engine_placeholder::asset
