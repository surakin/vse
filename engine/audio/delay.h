// A feedback delay and a feedback delay network reverb (Phase 26 Day 424).
#pragma once

#include "audio/effect.h"

namespace vse::audio
{

/// What a delay does.
///
/// - Thread-safety: thread-compatible plain data.
struct DelayDesc
{
    /// The delay in seconds (at most the maximum the effect was made with).
    f32 delay_seconds = 0.25f;
    /// The share of the delayed signal fed back, 0 to 0.995.
    f32 feedback = 0.4f;
    /// The delayed signal in the output.
    f32 wet = 0.5f;
    /// The input in the output.
    f32 dry = 1.0f;
    /// The damping of the repeats: 0 passes them unchanged, 1 filters them heavily (a one-pole low-pass in the loop).
    f32 damping = 0.0f;
};

/// Builds the block of a delay.
///
/// - Param sample_rate: the rate, in Hz.
/// - Param desc: the settings; out-of-range values are clamped.
/// - Returns: the block.
/// - Fails: never.
EffectParams MakeDelayParams(u32 sample_rate, const DelayDesc& desc);

/// Makes a delay effect: its buffer is allocated here for the longest delay it can be asked for.
///
/// - Param channels: 1 to 8.
/// - Param sample_rate: the rate, in Hz.
/// - Param max_delay_seconds: 0.001 to 10.
/// - Param ramp_frames: the frames over which a change of delay time glides (0: at once).
/// - Returns: the effect, or null on an invalid argument or an exhausted heap.
/// - Ownership: the caller owns the result until it is handed to `Mixer::SetInsert`.
/// - Fails: reported by a null result.
UniquePtr<Effect> MakeDelayEffect(u32 channels, u32 sample_rate, f32 max_delay_seconds, u32 ramp_frames);

/// What a reverb does. The rate and the size are fixed when the effect is made; the rest can change at run time.
///
/// - Thread-safety: thread-compatible plain data.
struct ReverbDesc
{
    /// The rate of the signal, in Hz.
    u32 sample_rate = 48000;
    /// Scales the delay lines: 1 is a medium room, 0.4 a small one, 3 a hall.
    f32 size = 1.0f;
    /// The time for the tail to fall by 60 dB, in seconds.
    f32 decay_seconds = 1.5f;
    /// How much the high frequencies die faster than the low ones, 0 to 1.
    f32 damping = 0.3f;
    /// The reverberated signal in the output.
    f32 wet = 0.3f;
    /// The input in the output.
    f32 dry = 1.0f;
    /// The delay before the reverb starts, in seconds, 0 to 0.25.
    f32 predelay_seconds = 0.0f;
};

/// The documented presets.
enum class ReverbPreset : u8
{
    /// A small room: short, quick to die.
    SmallRoom,
    /// A concert hall.
    Hall,
    /// A cave: long and dark.
    Cave,
};

/// The settings of a preset at a sample rate.
///
/// - Param preset: the preset.
/// - Param sample_rate: the rate, in Hz.
/// - Returns: the description.
/// - Fails: never.
ReverbDesc ReverbPresetDesc(ReverbPreset preset, u32 sample_rate);

/// The delay of each of the eight lines in frames: prime numbers scaled by the rate and the size.
///
/// - Param sample_rate: the rate, in Hz.
/// - Param size: the size.
/// - Param lengths: receives eight lengths.
/// - Fails: never.
void ReverbLineLengths(u32 sample_rate, f32 size, u32 lengths[8]);

/// Builds the block of a reverb from its description (the per-line gains that give the decay time, the damping, the
/// mix).
///
/// - Param desc: the settings; `sample_rate` and `size` must be those the effect was made with.
/// - Returns: the block.
/// - Fails: never.
EffectParams MakeReverbParams(const ReverbDesc& desc);

/// Makes a reverb effect (eight lines, a Hadamard feedback matrix, damping per line, pre-delay), initially with the
/// parameters of `desc`.
///
/// - Param channels: 1 to 8; the lines are spread over the channels, so two channels give a decorrelated stereo field.
/// - Param desc: the settings.
/// - Returns: the effect, or null on an invalid argument or an exhausted heap.
/// - Ownership: as for `MakeDelayEffect`.
/// - Fails: reported by a null result.
UniquePtr<Effect> MakeReverbEffect(u32 channels, const ReverbDesc& desc);

}  // namespace vse::audio
