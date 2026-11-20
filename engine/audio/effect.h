// The effect interface and the effects (Phase 26 Days 423-424): filters, a delay and a reverb, used as inserts on
// buses.
#pragma once

#include "audio/state_io.h"
#include "containers/unique_ptr.h"
#include "types.h"

namespace vse::audio
{

/// The number of floats an effect's parameter block holds.
inline constexpr u32 kEffectParams = 16;

/// The parameters of an effect, as a plain block of floats the control thread fills and the audio thread applies at a
/// block boundary. What the floats mean belongs to the effect; the `Make...Params` functions of each effect build them,
/// with every logarithm, power and trigonometric function computed on the control side.
///
/// - Thread-safety: thread-compatible plain data.
struct EffectParams
{
    /// The values.
    f32 v[kEffectParams] = {};
};

/// A processor of interleaved frames, in place: a filter, a delay or a reverb.
///
/// An effect is made on the control thread (all its memory is allocated then), handed to a bus through
/// `Mixer::SetInsert`, and from then on only the audio thread calls `Process`, `SetParams` and `Reset`. Every call on
/// the audio thread allocates nothing, locks nothing and uses `+ - * /` and `Sqrt` only.
///
/// - Thread-safety: thread-compatible; after hand-over only the audio thread calls the audio-side methods.
/// - Ownership: owned by the mixer once inserted (kept alive until the mixer is destroyed, even after it is cleared).
class Effect
{
public:
    virtual ~Effect() = default;

    /// Processes `frames` frames in place.
    ///
    /// - Param samples: `frames * Channels()` interleaved samples.
    /// - Param frames: the frame count.
    /// - Ownership: `samples` is borrowed for the call.
    /// - Fails: never; allocates nothing.
    virtual void Process(f32* samples, u32 frames) = 0;

    /// Starts moving to a new parameter block (audio thread, at a block boundary); the move is smoothed over the
    /// effect's ramp so a sweep does not click.
    ///
    /// - Param params: a block built by the effect's `Make...Params`.
    /// - Fails: never; a block that is out of range is clamped.
    virtual void SetParams(const EffectParams& params) = 0;

    /// Clears the internal state (the next frames start from silence).
    ///
    /// - Fails: never.
    virtual void Reset() = 0;

    /// How many frames after its input stops the effect still produces sound above the smallest float that is kept
    /// (below it the state is flushed to exact zero), for the given parameters. Control-side arithmetic (libm).
    ///
    /// - Param params: the parameters in effect.
    /// - Returns: the frames; at least 0.
    /// - Fails: never.
    virtual u32 TailFrames(const EffectParams& params) const = 0;

    /// The channels the effect processes.
    ///
    /// - Fails: never.
    virtual u32 Channels() const = 0;

    /// Writes the effect's running state (history, delay lines, ramps) for a snapshot; the parameters are part of it.
    ///
    /// - Param out: the writer.
    /// - Fails: never.
    /// - Ownership: `out` is borrowed for the call.
    virtual void SaveState(StateOut* out) const = 0;

    /// Reads what `SaveState` wrote, into an effect of the same kind and size.
    ///
    /// - Param in: the reader; it fails by its flag when the data do not fit this effect.
    /// - Fails: reported by the reader's flag.
    /// - Ownership: `in` is borrowed for the call.
    virtual void LoadState(StateIn* in) = 0;
};

}  // namespace vse::audio
