#pragma once

//! Mip generation for the texture minter: a deterministic filter in exact integer arithmetic, so the same input gives
//! byte-identical output on every compiler, machine and thread count. Phase 22 Day 323.
//!
//! The formats are `R8_UNorm`, `RG8_UNorm`, `RGBA8_UNorm`, `RGBA8_SRGB`, `R16_Float`, `RGBA16_Float`. An sRGB texture is
//! filtered in linear light (a 16-bit linear value per channel, through a fixed table). Half-float texels are exact
//! multiples of 2^-24 and are filtered as such integers. Nothing here uses floating-point arithmetic.
//!
//! A level of `w x h` texels gives a next level of `max(1, w / 2) x max(1, h / 2)`. Destination texel `i` of an axis
//! (source length `s`, destination length `d`) covers the source range `[i * s / d, (i + 1) * s / d)` (integer
//! division), so an odd length gives its last texel a range of three. The box filter averages that range; the Kaiser
//! filter is the fixed 6-tap kernel `-5 24 109 109 24 -5` (sum 256, a Kaiser-windowed sinc with beta 4) over the
//! source texels `lo - 2 .. lo + 3` (`lo` the start of the range), edges clamped; an axis of length 1 is copied.

#include "asset/status.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "rhi/format.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// The most texels along one side of a texture this version accepts.
inline constexpr u32 kMaxTextureSize = 16384;

/// The filter that makes a mip level from the one above it.
enum class MipFilter : u32
{
    Box = 0,     ///< The mean of the covered texels.
    Kaiser = 1,  ///< The fixed 6-tap Kaiser-windowed kernel (sharper than the box; may overshoot and is clamped).
};

/// How the alpha channel is treated while filtering.
enum class MipAlpha : u32
{
    Straight = 0,     ///< 8-bit RGBA only: colors are weighted by alpha while filtering and stored straight.
    Premultiply = 1,  ///< The level 0 colors were multiplied by alpha (`PremultiplyAlphaRgba8`); all levels are filtered
                      ///< as plain values and stay premultiplied.
    Ignore = 2,       ///< Every channel is filtered as it is.
};

/// The options of a mip chain.
///
/// - Thread-safety: thread-compatible (a plain value).
struct MipOptions
{
    MipFilter filter = MipFilter::Box;  ///< The filter.
    MipAlpha alpha = MipAlpha::Straight;  ///< The alpha treatment (only `Ignore` and `Premultiply` for half floats).
};

/// Returns whether `format` is one the mip generator and the texture blob handle in this version.
///
/// - Fails: never.
bool IsTextureFormat(rhi::Format format);

/// Returns the number of levels of a full chain: `floor(log2(max(width, height))) + 1`.
///
/// - Fails: never.
u32 FullMipCount(u32 width, u32 height);

/// Returns the size of a level of `width x height` texels in `format`, rows tightly packed (0 for an unsupported format
/// or a size of 0).
///
/// - Fails: never.
u64 TightLevelBytes(rhi::Format format, u32 width, u32 height);

/// Multiplies the color channels of 8-bit RGBA texels by alpha, in linear light for sRGB data.
///
/// - Param srgb: whether the color channels are sRGB-encoded.
/// - Param pixels: `RGBA8` texels; a whole number of texels.
/// - Fails: `Invalid` when the size is not a multiple of 4.
/// - Ownership: borrows `pixels` for the call.
Status PremultiplyAlphaRgba8(bool srgb, MutableByteSpan pixels);

/// Converts 8-bit RGBA texels into `format`: `R8_UNorm` takes red, `RG8_UNorm` red and green, `RGBA8_*` copies, and the
/// half-float formats take linear values (sRGB color channels are decoded when `source_srgb`; alpha is always linear).
///
/// - Param format: a supported format.
/// - Param source_srgb: whether the color channels of `rgba` are sRGB-encoded.
/// - Param rgba: `width * height` texels.
/// - Param out: replaced by the converted texels, tightly packed.
/// - Fails: `Invalid` for an unsupported format or a wrong size; `LimitExceeded` when memory runs out.
/// - Ownership: borrows `rgba` for the call; `*out` is replaced.
Status ConvertRgba8(rhi::Format format, bool source_srgb, u32 width, u32 height, ByteSpan rgba, Vector<byte>* out);

/// Makes the full mip chain of an image: level 0 is copied, then each level is filtered from the one above.
///
/// - Param format: a supported format.
/// - Param width: 1 to [[asset::kMaxTextureSize]].
/// - Param height: 1 to [[asset::kMaxTextureSize]].
/// - Param level0: `TightLevelBytes(format, width, height)` bytes.
/// - Param options: the filter and alpha treatment (`Straight` needs `RGBA8_*`).
/// - Param chain: replaced by all levels, level 0 first, each tightly packed.
/// - Fails: `Invalid` for an unsupported format, a wrong size, `Straight` on a format without 8-bit alpha, or a
///   half-float texel that is infinite or NaN; `LimitExceeded` when memory runs out.
/// - Ownership: borrows `level0` for the call; `*chain` is replaced.
Status GenerateMipChain(rhi::Format format, u32 width, u32 height, ByteSpan level0, const MipOptions& options,
                        Vector<byte>* chain);

}  // namespace engine_placeholder::asset
