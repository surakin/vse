// Filters (Phase 26 Day 423): a biquad of the RBJ cookbook types and a one-pole low-pass.
#pragma once

#include "audio/effect.h"

namespace engine_placeholder::audio
{

/// The response of a biquad.
enum class FilterType : u8
{
    /// Passes below the cutoff.
    LowPass,
    /// Passes above the cutoff.
    HighPass,
    /// Passes around the cutoff (0 dB peak).
    BandPass,
    /// Removes the cutoff.
    Notch,
    /// Boosts or cuts around the cutoff by `gain_db`.
    Peaking,
    /// Boosts or cuts below the cutoff by `gain_db`.
    LowShelf,
    /// Boosts or cuts above the cutoff by `gain_db`.
    HighShelf,
};

/// The coefficients of a biquad as the control side designs them.
///
/// - Thread-safety: thread-compatible plain data.
struct FilterDesign
{
    /// The block for `Effect::SetParams`.
    EffectParams params;
    /// Whether an input was out of range and was clamped (a frequency above Nyquist, a Q below 0.1, ...), or the filter
    /// would have been unstable and was replaced by the identity.
    bool clamped = false;
};

/// Designs a biquad.
///
/// - Param type: the response.
/// - Param sample_rate: the rate of the signal, in Hz.
/// - Param cutoff_hz: the cutoff or centre, clamped to 10 Hz .. 0.49 of the rate.
/// - Param q: the quality, clamped to 0.1 .. 20 (0.7071 is a Butterworth low-pass).
/// - Param gain_db: for the peaking and shelf types, clamped to -40 .. 40.
/// - Returns: the block (coefficients normalised by `a0`) and whether anything was clamped. A design whose poles are
/// not
///   inside the unit circle comes back as the identity with `clamped` set.
/// - Fails: never.
FilterDesign DesignFilter(FilterType type, u32 sample_rate, f32 cutoff_hz, f32 q, f32 gain_db);

/// The identity biquad (the filter passes the signal untouched and bit for bit).
///
/// - Fails: never.
EffectParams IdentityFilterParams();

/// The magnitude of a biquad's response at a frequency, from a block built by `DesignFilter`.
///
/// - Param params: the block.
/// - Param sample_rate: the rate, in Hz.
/// - Param frequency_hz: the frequency.
/// - Returns: the linear gain.
/// - Fails: never.
f32 FilterMagnitude(const EffectParams& params, u32 sample_rate, f32 frequency_hz);

/// Makes a biquad effect, initially the identity.
///
/// - Param channels: 1 to 8.
/// - Param ramp_frames: the frames over which a parameter change is interpolated (0: at once).
/// - Returns: the effect, or null if the arguments are invalid or the heap is exhausted.
/// - Ownership: the caller owns the result until it is handed to `Mixer::SetInsert`.
/// - Fails: reported by a null result.
UniquePtr<Effect> MakeFilterEffect(u32 channels, u32 ramp_frames);

/// The block of a one-pole low-pass.
///
/// - Param sample_rate: the rate, in Hz.
/// - Param cutoff_hz: the -3 dB frequency; at or above 0.49 of the rate the filter passes everything.
/// - Returns: the block.
/// - Fails: never.
EffectParams OnePoleParams(u32 sample_rate, f32 cutoff_hz);

/// Makes a one-pole low-pass effect (6 dB per octave, one multiply per sample), initially open.
///
/// - Param channels: 1 to 8.
/// - Param ramp_frames: as for `MakeFilterEffect`.
/// - Returns: the effect, or null on an invalid argument or an exhausted heap.
/// - Ownership: as for `MakeFilterEffect`.
/// - Fails: reported by a null result.
UniquePtr<Effect> MakeOnePoleEffect(u32 channels, u32 ramp_frames);

}  // namespace engine_placeholder::audio
