// Sample and channel conversions (Phase 26 Day 416): integer samples to `f32` and back, interleaving, and channel-count
// conversion by a gain matrix. Everything here uses `+ - * /`, comparisons and `Round` only, allocates nothing and is
// safe on the audio thread (spec section 4 and the module rules).
#pragma once

#include "audio/format.h"
#include "containers/span.h"
#include "types.h"

namespace engine_placeholder::audio
{

/// The ITU-R BS.775 gain for folding a center or surround channel into a front channel: 1/sqrt(2).
inline constexpr f32 kDownmixGain = 0.70710678118654752f;

/// The integer-to-float scales: a sample of `N` bits maps to `value / 2^(N-1)`, so full negative scale is exactly -1.0
/// and full positive scale is just under +1.0; unsigned 8-bit is `(value - 128) / 128`.
///
/// - Param value: the sample.
/// - Returns: the float, in [-1, 1).
/// - Thread-safety: thread-safe; pure.
f32 U8ToF32(u8 value);
/// See [[audio::U8ToF32]]: 16-bit, scale 1/32768; exact.
f32 S16ToF32(i16 value);
/// See [[audio::U8ToF32]]: 24-bit held in an `i32` in [-8388608, 8388607], scale 1/8388608; exact.
f32 S24ToF32(i32 value);
/// See [[audio::U8ToF32]]: 32-bit, scale 1/2147483648; rounded to the nearest `f32`.
f32 S32ToF32(i32 value);

/// The float-to-integer rule, written once: multiply by `2^(N-1)`, round to the nearest integer with halves away from
/// zero, clamp to the range of the type (so +1.0 gives the largest value and anything beyond +-1 clips), and give
/// silence for a NaN. No dither.
///
/// - Param value: the sample.
/// - Returns: the integer sample.
/// - Thread-safety: thread-safe; pure.
u8 F32ToU8(f32 value);
/// See [[audio::F32ToU8]]: 16-bit.
i16 F32ToS16(f32 value);
/// See [[audio::F32ToU8]]: 24-bit, in an `i32` in [-8388608, 8388607].
i32 F32ToS24(f32 value);
/// See [[audio::F32ToU8]]: 32-bit.
i32 F32ToS32(f32 value);

/// Converts samples of any format to `f32`.
///
/// - Param format: the format of `in`.
/// - Param in: the bytes; its size must be `BytesPerSample(format) * out.Size()`.
/// - Param out: receives one float per sample.
/// - Returns: false (and writes nothing) if the sizes disagree; true otherwise.
/// - Fails: reported by return value.
/// - Thread-safety: thread-safe when the buffers are not shared.
/// - Ownership: borrows both buffers for the call.
bool ConvertToF32(SampleFormat format, ByteSpan in, Span<f32> out);

/// Converts `f32` samples to any format by the rule of [[audio::F32ToU8]].
///
/// - Param format: the format to write.
/// - Param in: the samples.
/// - Param out: receives the bytes; its size must be `BytesPerSample(format) * in.Size()`.
/// - Returns: false (and writes nothing) if the sizes disagree; true otherwise.
/// - Fails: reported by return value.
/// - Thread-safety: thread-safe when the buffers are not shared.
/// - Ownership: borrows both buffers for the call.
bool ConvertFromF32(SampleFormat format, Span<const f32> in, MutableByteSpan out);

/// Interleaves planar samples: `planar` holds channel 0's frames, then channel 1's, and so on.
///
/// - Param planar: `channels * frames` samples.
/// - Param out: receives `frames` frames of `channels` samples each; the same size as `planar`.
/// - Param channels: at least 1.
/// - Returns: false (and writes nothing) if the sizes disagree or are not a multiple of `channels`.
/// - Fails: reported by return value.
/// - Thread-safety: thread-safe when the buffers are not shared.
/// - Ownership: borrows both buffers for the call; they must not overlap.
bool Interleave(Span<const f32> planar, Span<f32> out, u32 channels);

/// The inverse of [[audio::Interleave]].
///
/// - Param interleaved: `frames` frames of `channels` samples.
/// - Param planar: receives channel 0's frames, then channel 1's, and so on; the same size as `interleaved`.
/// - Param channels: at least 1.
/// - Returns: false (and writes nothing) if the sizes disagree or are not a multiple of `channels`.
/// - Fails: reported by return value.
/// - Thread-safety: thread-safe when the buffers are not shared.
/// - Ownership: borrows both buffers for the call; they must not overlap.
bool Deinterleave(Span<const f32> interleaved, Span<f32> planar, u32 channels);

/// A table of gains that turns a frame of one layout into a frame of another: `gain[out][in]`.
///
/// - Thread-safety: thread-compatible plain data.
struct ChannelMatrix
{
    /// The channels of the source layout.
    u32 inputs = 0;
    /// The channels of the destination layout.
    u32 outputs = 0;
    /// `gain[o][i]` is how much of source channel `i` goes into destination channel `o`.
    f32 gain[kMaxChannels][kMaxChannels] = {};
};

/// Builds the table that converts `from` to `to`.
///
/// The rules: the same layout is the identity; a speaker the destination has goes to it with gain 1; mono goes to front
/// left and front right with gain 1 each (a copy); a layout goes to mono through stereo with the gain 0.5 on each side
/// (an average); a speaker the destination lacks is folded ITU-style with `kDownmixGain` (front center into both
/// fronts, a back or side speaker into the front on its side or, for a side speaker, into the back on its side when the
/// destination has one) and the LFE channel is dropped. Folding adds, so a loud 5.1 frame can exceed 1.0; the master
/// limiter deals with it.
///
/// - Param from: the source layout.
/// - Param to: the destination layout.
/// - Returns: the table.
/// - Thread-safety: thread-safe; pure.
ChannelMatrix BuildChannelMatrix(ChannelLayout from, ChannelLayout to);

/// Converts interleaved frames through a table; each output is the sum over the inputs in channel order.
///
/// - Param matrix: the table.
/// - Param in: `frames * matrix.inputs` samples.
/// - Param out: receives `frames * matrix.outputs` samples.
/// - Param frames: the number of frames.
/// - Returns: false (and writes nothing) if a buffer is too small.
/// - Fails: reported by return value.
/// - Thread-safety: thread-safe when the buffers are not shared.
/// - Ownership: borrows both buffers for the call; they must not overlap.
bool ApplyChannelMatrix(const ChannelMatrix& matrix, Span<const f32> in, Span<f32> out, usize frames);

}  // namespace engine_placeholder::audio
