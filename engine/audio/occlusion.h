// Occlusion (Phase 26 Day 431): how much geometry sits between an emitter and the listener. The audio module knows only
// this interface; `engine/audio_physics` implements it over the physics world.
#pragma once

#include "math/vector.h"
#include "types.h"

namespace vse::audio
{

/// Answers how blocked the straight path between two points is.
///
/// - Thread-safety: thread-compatible; called from the control thread only.
/// - Ownership: owned by whoever made it; `SpatialAudio` borrows it.
class OcclusionQuery
{
public:
    virtual ~OcclusionQuery() = default;

    /// Tests the path from `from` to `to`.
    ///
    /// - Param from: where the sound starts (the emitter).
    /// - Param to: where it is heard (the listener).
    /// - Param crossed: receives how many colliders the path crosses (may be null).
    /// - Returns: 0 for an open path to 1 for one that is blocked completely.
    /// - Fails: returns 0 when it cannot answer.
    /// - Ownership: `crossed` is borrowed for the call.
    virtual f32 Occlusion(const Vec3d& from, const Vec3d& to, u32* crossed) = 0;
};

/// How occlusion is tested, smoothed and heard.
///
/// - Thread-safety: thread-compatible plain data.
struct OcclusionConfig
{
    /// How often one emitter is tested at most, per second.
    f32 tests_per_second = 4.0f;
    /// The most tests in one update, over all emitters; the rest keep their last value.
    u32 max_tests_per_update = 8;
    /// The time constant of the smoothing in seconds: a door that opens is 63 percent open after this long.
    f32 smooth_seconds = 0.15f;
    /// The gain factor at full occlusion.
    f32 occluded_gain = 0.35f;
    /// The low-pass cutoff in Hz at full occlusion (the air cutoff is used when it is lower).
    f32 occluded_cutoff_hz = 900.0f;
};

}  // namespace vse::audio
