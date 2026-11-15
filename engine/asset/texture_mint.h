#pragma once

//! Minting a texture blob from decoded pixels.
//!
//! Phase 22 Day 338. The core of the texture minter, in the engine so that `asset_mint` (from `.tex.src` and raw
//! images) and the glTF importer (from decoded PNG and JPEG) make identical blobs: 8-bit RGBA layers are premultiplied
//! if asked, converted to the storage format, filtered into a mip chain in exact integer arithmetic and, for the BC
//! formats, block-compressed, all deterministically.

#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "asset/texture.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "jobs/scheduler.h"
#include "types.h"

namespace engine_placeholder::asset
{

/// What a texture is made as.
///
/// - Thread-safety: thread-compatible plain data.
struct TextureMintSpec
{
    TextureKind kind = TextureKind::Tex2D;          ///< The kind.
    rhi::Format format = rhi::Format::RGBA8_UNorm;  ///< The storage format (the sRGB variant already chosen).
    bool drop_alpha = false;                        ///< BC7: encode alpha as 255.
    bool srgb = false;                              ///< The source colors are sRGB.
    u32 mips = 0;                                   ///< The stored mip count, 0 for the full chain.
    MipFilter filter = MipFilter::Box;              ///< The mip filter.
    MipAlpha alpha = MipAlpha::Straight;            ///< The alpha treatment.
};

/// Mints a texture blob from tightly packed RGBA8 layers.
///
/// - Param types: the registry; it must hold `TEXR`.
/// - Param id: the texture's id.
/// - Param spec: kind, format, color space, mips, filter, alpha.
/// - Param width: texels across (1 to 16384).
/// - Param height: texels down.
/// - Param images: one `width * height * 4` byte layer per layer of the kind (1, 6 for a cube, any for an array).
/// - Param out: replaced by the blob.
/// - Param target: the API layout of the blob (0 for the default).
/// - Param scheduler: runs the block compression on workers when not null.
/// - Fails: `Invalid` (a layer of another size, a cube that is not square, a mip count above the full chain),
/// `LimitExceeded` (memory), and what
///   [[asset::BuildTextureBlob]] reports.
/// - Ownership: borrows its arguments for the call only; `*out` is replaced.
Status MintTextureFromPixels(const TypeRegistry& types, ResourceId id, const TextureMintSpec& spec, u32 width,
                             u32 height, Span<const ByteSpan> images, Vector<byte>* out, u32 target = 0,
                             jobs::Scheduler* scheduler = nullptr);

}  // namespace engine_placeholder::asset
