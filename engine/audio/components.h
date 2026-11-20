// The audio components (Phase 26 Day 428): what an entity carries to make sound in the world and to hear it.
#pragma once

#include "audio/clip.h"
#include "audio/distance.h"
#include "audio/mixer.h"
#include "math/vector.h"
#include "types.h"

namespace vse::audio
{

/// A sound at the entity's `ecs::WorldTransform`: the position is the entity's, the axis of its cone is the entity's
/// local -Z.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: shares `clip` and `stream`.
struct AudioSource
{
    /// The sound; null plays nothing (unless `stream` is set).
    AudioClipConstPtr clip;
    /// A stream the voice reads instead of `clip` (a live voice fed by `StreamBuffer::Push`, for instance); null for a
    /// clip. It is never also the stream of another source (a push stream has one reader). The voice starts even when
    /// the emitter is not audible (virtual), as a looping clip does, so that the stream keeps being read.
    StreamBufferPtr stream;
    /// The bus the voice mixes into.
    BusId bus = {0};
    /// The linear gain before distance and cone, 0 to 16.
    f32 gain = 1.0f;
    /// The rate ratio.
    f32 pitch = 1.0f;
    /// Whether the voice loops.
    bool loop = false;
    /// Whether the voice starts when the emitter is first seen (a one-shot starts when it is first audible).
    bool play_on_start = true;
    /// Whether the sound is placed in the world; false plays it flat (music, interface sounds).
    bool spatial = true;
    /// Whether Doppler shift applies (Day 430).
    bool doppler = true;
    /// Whether occlusion applies (Day 431).
    bool occlusion = true;
    /// Higher survives when voices are scarce and breaks a tie of loudness.
    i32 priority = 0;
    /// How the gain falls with distance.
    Attenuation attenuation;
    /// The cone of sound.
    Cone cone;
};

/// An explicit velocity for an emitter (a physics body's, for instance), in units per second. Without it the Doppler
/// effect uses the change of the entity's position between updates.
///
/// - Thread-safety: thread-compatible plain data.
struct AudioVelocity
{
    /// The velocity.
    Vec3 velocity;
};

/// The ears: the active listener's position, orientation and velocity (from the transform's change between updates)
/// place every spatial source. The orientation is the entity's: forward is local -Z, right +X, up +Y.
///
/// - Thread-safety: thread-compatible plain data.
struct AudioListener
{
    /// The linear gain applied to everything heard, 0 to 4.
    f32 gain = 1.0f;
    /// Whether this listener is a candidate; the active one with the lowest entity id is used.
    bool active = true;
};

}  // namespace vse::audio
