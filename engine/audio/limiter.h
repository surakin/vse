// The master limiter and the decibel helpers (Phase 26 Day 422).
#pragma once

#include "containers/vector.h"
#include "types.h"

namespace engine_placeholder::audio
{

/// Converts decibels to a linear gain: `10^(db / 20)`. Control-side maths (libm through `lang/scalar.h`); the mixer
/// itself takes linear gains.
///
/// - Param db: the level in dB; -infinity or a very negative value gives 0.
/// - Returns: the linear gain.
/// - Fails: never.
f32 DbToGain(f32 db);

/// Converts a linear gain to decibels: `20 log10(gain)`.
///
/// - Param gain: the linear gain; 0 or less gives -infinity (`-1000` is returned instead, so the result stays finite).
/// - Returns: the level in dB.
/// - Fails: never.
f32 GainToDb(f32 gain);

/// What a limiter does.
///
/// - Thread-safety: thread-compatible plain data.
struct LimiterConfig
{
    /// The peak the output is held to, as a linear level (0.9 is about -0.9 dBFS).
    f32 threshold = 0.9f;
    /// The look-ahead in frames: the signal is delayed by this much and the gain falls to the needed level across it.
    u32 lookahead_frames = 64;
    /// The time the gain takes to recover, in frames (an exponential with this time constant).
    f32 release_frames = 4800.0f;
};

/// A look-ahead peak limiter on interleaved frames.
///
/// For each frame the limiter looks at the loudest sample of the next `lookahead_frames` frames, asks for the gain
/// that holds it to the threshold, averages that gain over the look-ahead (so the gain moves in a ramp, not a step) and
/// releases exponentially. Since every gain in the average was asked for by a window that contains the frame being
/// output, **the output never exceeds the threshold**. Below the threshold the gain is exactly 1, so the signal passes
/// bit for bit, delayed by the look-ahead. Arithmetic is `+ - * /` only.
///
/// - Thread-safety: thread-compatible; one thread processes.
/// - Ownership: owns its delay line and histories; allocates in `Init` only.
class Limiter
{
public:
    /// Sets the limiter up for `channels` channels.
    ///
    /// - Param channels: 1 to 8.
    /// - Param config: the settings; the threshold must be above 0 and the look-ahead at most 4096.
    /// - Returns: false if the configuration is invalid or the heap is exhausted.
    /// - Fails: reported by return value.
    bool Init(u32 channels, const LimiterConfig& config);

    /// Limits `frames` frames in place.
    ///
    /// - Param samples: `frames * channels` interleaved samples, overwritten with the delayed, limited signal.
    /// - Param frames: the frame count.
    /// - Ownership: `samples` is borrowed for the call.
    /// - Fails: never; allocates nothing.
    void Process(f32* samples, u32 frames);

    /// Clears the history (the next frames start from silence).
    ///
    /// - Fails: never.
    void Reset();

    /// The delay the limiter adds, in frames.
    u32 LatencyFrames() const { return config_.lookahead_frames; }

    /// The lowest gain applied in the last `Process` call (1 when nothing was limited).
    f32 MinGain() const { return min_gain_; }

private:
    LimiterConfig config_;
    u32 channels_ = 0;
    u32 position_ = 0;
    u32 delay_position_ = 0;
    f32 gain_ = 1.0f;
    f32 release_coef_ = 0.0f;
    f32 min_gain_ = 1.0f;
    Vector<f32> delay_;
    Vector<f32> peaks_;
    Vector<f32> gains_;
};

}  // namespace engine_placeholder::audio
