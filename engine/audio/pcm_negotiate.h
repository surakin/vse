// Choosing a hardware configuration (Phase 26 Day 425): what a sound card offers against what the game asked for. The
// logic is portable and tested against recorded capability tables; the platform backends fill the table from the
// operating system and apply the grant.
#pragma once

#include "audio/device.h"

namespace engine_placeholder::audio
{

/// The sample format a card plays.
enum class PcmFormat : u8
{
    /// 32-bit float: no conversion needed.
    F32,
    /// 32-bit signed integer.
    S32,
    /// 16-bit signed integer.
    S16,
};

/// What a card can do.
///
/// - Thread-safety: thread-compatible plain data.
struct PcmCaps
{
    /// The lowest and highest sample rate, in Hz.
    u32 rate_min = 0;
    /// See `rate_min`.
    u32 rate_max = 0;
    /// The fewest and most channels.
    u32 channels_min = 0;
    /// See `channels_min`.
    u32 channels_max = 0;
    /// Whether the card takes 32-bit float.
    bool has_f32 = false;
    /// Whether the card takes 32-bit integers.
    bool has_s32 = false;
    /// Whether the card takes 16-bit integers.
    bool has_s16 = false;
    /// The smallest and largest period (one render call), in frames.
    u32 period_min = 0;
    /// See `period_min`.
    u32 period_max = 0;
    /// The fewest and most periods in the buffer.
    u32 periods_min = 0;
    /// See `periods_min`.
    u32 periods_max = 0;
    /// The smallest and largest whole buffer, in frames.
    u32 buffer_min = 0;
    /// See `buffer_min`.
    u32 buffer_max = 0;
};

/// What `NegotiatePcm` chose.
///
/// - Thread-safety: thread-compatible plain data.
struct PcmGrant
{
    /// The rate in Hz.
    u32 rate = 0;
    /// The channel count.
    u32 channels = 0;
    /// The sample format.
    PcmFormat format = PcmFormat::F32;
    /// Frames per period (per render call).
    u32 period_frames = 0;
    /// Periods in the buffer.
    u32 periods = 0;
};

/// Picks the configuration closest to the request: the wanted rate and channel count clamped into the card's range
/// (48000 for a rate of 0), the best format it takes (float, then 32-bit, then 16-bit), the period nearest the wanted
/// size and three periods in the buffer, fewer if the buffer limit asks for it (never fewer than two).
///
/// - Param caps: what the card offers.
/// - Param want_rate: the wanted rate in Hz; 0 for the default.
/// - Param want_channels: the wanted channel count.
/// - Param want_period: the wanted frames per render call.
/// - Param out: receives the grant.
/// - Ownership: `out` is borrowed for the call.
/// - Returns: `Ok`; `Unsupported` if the table is empty or inconsistent, no format is offered, or no whole buffer of
/// two
///   periods fits the card's limits.
/// - Fails: `Unsupported`; `out` is not written then.
DeviceStatus NegotiatePcm(const PcmCaps& caps, u32 want_rate, u32 want_channels, u32 want_period, PcmGrant* out);

/// Converts rendered `f32` frames to the card's sample format, interleaved, for writing.
///
/// - Param format: the card's format.
/// - Param in: `samples` floats.
/// - Param samples: the number of samples.
/// - Param out: receives the converted samples (4 bytes each for `F32` and `S32`, 2 for `S16`).
/// - Ownership: `in` and `out` are borrowed for the call.
/// - Fails: never.
void ConvertForPcm(PcmFormat format, const f32* in, u32 samples, void* out);

/// The layout a card's channel count stands for (Day 440: shared by the ALSA and WASAPI backends).
///
/// - Param channels: the channel count the card plays.
/// - Param out: receives the layout; not written when there is none.
/// - Ownership: `out` is borrowed for the call.
/// - Returns: true for 1, 2, 4, 6 and 8 channels (mono, stereo, quad, 5.1, 7.1); false for any other count.
/// - Fails: reported by the return value.
bool LayoutOf(u32 channels, ChannelLayout* out);

/// The bytes one sample takes in a format.
///
/// - Param format: the format.
/// - Returns: 2 or 4.
/// - Fails: never.
inline u32 PcmSampleBytes(PcmFormat format)
{
    return format == PcmFormat::S16 ? 2u : 4u;
}

}  // namespace engine_placeholder::audio
