#pragma once

//! Block compression for the texture minter: decoders for BC1, BC2, BC3, BC4, BC5 and BC7, and encoders for BC1, BC4,
//! BC5 and BC7, in exact integer arithmetic so the output is byte-identical on every compiler, machine and worker
//! count. Phase 22 Day 324.
//!
//! A block is 4x4 texels, row-major, stored in 8 bytes (BC1, BC4) or 16 bytes (BC2, BC3, BC5, BC7). The decoders give
//! 8-bit RGBA texels (BC4 puts its channel in red and BC5 its two in red and green; the others read 0 and alpha 255).
//! The interpolated colors of BC1 are `(2a + b) / 3` and `(a + b) / 2` of the 8-bit expanded endpoints, truncated: the
//! decoder is the reference of this engine, GPUs differ by at most one step. The encoders choose among endpoint sets by
//! the sum of squared channel errors and break every tie in favor of the lower index or the first candidate tried, so
//! the result depends on the input texels only. A partial block at the right or bottom edge repeats its last column and
//! row.

#include "asset/status.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "jobs/scheduler.h"
#include "rhi/format.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// A block-compression scheme the decoders know.
enum class BcKind : u32
{
    Bc1 = 0,  ///< Color with 1-bit alpha, 8 bytes.
    Bc2 = 1,  ///< Color with 4-bit explicit alpha, 16 bytes (decode only).
    Bc3 = 2,  ///< Color with interpolated alpha, 16 bytes.
    Bc4 = 3,  ///< One channel, 8 bytes.
    Bc5 = 4,  ///< Two channels, 16 bytes.
    Bc7 = 5,  ///< High-quality color with alpha, 16 bytes (eight modes to decode; the encoder writes mode 6).
};

/// The encoder's effort: more refinement passes find lower errors and cost more time.
enum class BcQuality : u32
{
    Fast = 0,    ///< The range fit only.
    Normal = 1,  ///< The range fit and two refinement passes.
    High = 2,    ///< The range fit and four refinement passes.
};

/// The options of an encode.
///
/// - Thread-safety: thread-compatible (a plain value).
struct BcEncodeOptions
{
    BcQuality quality = BcQuality::Normal;  ///< The effort.
    bool punchthrough_alpha = false;        ///< BC1: texels with alpha below 128 become transparent (the 3-color mode).
    bool drop_alpha = false;                ///< BC7: the alpha channel is encoded as 255 whatever the texels hold.
};

/// The bytes of one 4x4 block of `kind`: 8 or 16.
///
/// - Fails: never.
u32 BcBlockBytes(BcKind kind);

/// Returns whether `format` is a block-compressed format the minter stores in this version (BC1, BC4, BC5 and BC7).
///
/// - Fails: never.
bool IsBcFormat(rhi::Format format);

/// Maps a stored format to its scheme.
///
/// - Param format: a format for which `IsBcFormat` holds.
/// - Aborts: `format` is not one of those.
BcKind BcKindOf(rhi::Format format);

/// Decodes one block into 16 RGBA8 texels (64 bytes, row-major).
///
/// - Param kind: the scheme.
/// - Param block: `BcBlockBytes(kind)` bytes.
/// - Param rgba: receives 64 bytes.
/// - Fails: never; every bit pattern decodes.
/// - Ownership: borrows `block` and `rgba` for the call.
void DecodeBlock(BcKind kind, const byte* block, byte* rgba);

/// Decodes a whole image of blocks (rows of `ceil(width / 4)` blocks, tightly packed) into tight RGBA8 texels.
///
/// - Param kind: the scheme.
/// - Param width: 1 to [[asset::kMaxTextureSize]].
/// - Param height: 1 to [[asset::kMaxTextureSize]].
/// - Param blocks: exactly `ceil(w / 4) * ceil(h / 4) * BcBlockBytes(kind)` bytes.
/// - Param rgba: replaced by `width * height * 4` bytes.
/// - Fails: `Invalid` for a wrong size; `LimitExceeded` when memory runs out.
/// - Ownership: borrows `blocks` for the call; `*rgba` is replaced.
Status DecodeImage(BcKind kind, u32 width, u32 height, ByteSpan blocks, Vector<byte>* rgba);

/// Encodes one block from 16 RGBA8 texels (64 bytes, row-major) into `BcBlockBytes(kind)` bytes.
///
/// - Param kind: `Bc1`, `Bc4`, `Bc5` or `Bc7`.
/// - Param rgba: 64 bytes.
/// - Param options: the effort and the BC1 alpha rule.
/// - Param block: receives the block.
/// - Aborts: `kind` is not one of the four encodable schemes.
/// - Ownership: borrows `rgba` and `block` for the call.
void EncodeBlock(BcKind kind, const byte* rgba, const BcEncodeOptions& options, byte* block);

/// Encodes a whole image: tight RGBA8 texels in, rows of tightly packed blocks out. Block rows are spread over
/// `scheduler` when it is not null (the output does not depend on the worker count).
///
/// - Param kind: `Bc1`, `Bc4`, `Bc5` or `Bc7`.
/// - Param width: 1 to [[asset::kMaxTextureSize]].
/// - Param height: 1 to [[asset::kMaxTextureSize]].
/// - Param rgba: `width * height * 4` bytes.
/// - Param options: the effort and the BC1 alpha rule.
/// - Param scheduler: the job scheduler to fan out on, or null to encode on the calling thread; when given, the caller
///   must be its participant 0 outside any job.
/// - Param blocks: replaced by the blocks.
/// - Fails: `Invalid` for an unsupported scheme or a wrong size; `LimitExceeded` when memory runs out.
/// - Thread-safety: thread-compatible; the workers write disjoint parts of `*blocks`.
/// - Ownership: borrows `rgba` and `scheduler` for the call; `*blocks` is replaced.
Status EncodeImage(BcKind kind, u32 width, u32 height, ByteSpan rgba, const BcEncodeOptions& options,
                   jobs::Scheduler* scheduler, Vector<byte>* blocks);

}  // namespace engine_placeholder::asset
