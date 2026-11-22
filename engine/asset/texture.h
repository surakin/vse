#pragma once

//! The texture blob (type `TEXR`, version 1): every mip level of every layer, laid out the way the upload wants, built
//! by [[asset::BuildTextureBlob]] and read zero-copy through [[asset::TextureView]].
//!
//! Sections (ids from 16, little-endian, each 64-byte aligned by the container): 16 header (64 bytes), 17 the level
//! table (16 bytes per subresource: `u64` offset into the data section, `u32` size, `u32` row pitch; subresource
//! `layer * mip_count + mip`), 18 the data. There is no "generate at load" flag: every level is stored, and the mip
//! filter, the sRGB handling and the premultiplication happened in the minter. Each subresource starts at a multiple
//! of the recorded subresource alignment (an offset in the data section, which the container aligns to 64 bytes) and
//! its rows are `row_pitch` bytes apart (the row size rounded up to the row alignment and the texel size), so the
//! loader hands the spans to the upload unchanged. The alignments depend on the target API (`TextureLayoutForTarget`);
//! `RetargetTextureBlob` re-lays a blob out for another target.

#include "asset/blob.h"
#include "asset/resource_id.h"
#include "asset/resource_type.h"
#include "asset/status.h"
#include "asset/texture_mips.h"
#include "containers/span.h"
#include "containers/vector.h"
#include "rhi/desc.h"
#include "rhi/format.h"
#include "types.h"

namespace vse::asset
{

/// Most layers of an array texture.
inline constexpr u32 kMaxTextureLayers = 2048;
/// The layers of a cube map: +X, -X, +Y, -Y, +Z, -Z.
inline constexpr u32 kCubeFaces = 6;

/// The section ids of a texture blob.
enum TextureSection : u32
{
    kTextureSectionHeader = 16,  ///< Kind, format, size, counts, alignments (64 bytes).
    kTextureSectionLevels = 17,  ///< The subresource table, 16 bytes each.
    kTextureSectionData = 18,    ///< The texels of every subresource.
};

/// What a texture is.
enum class TextureKind : u32
{
    Tex2D = 0,   ///< One 2D image (one layer).
    Cube = 1,    ///< Six square 2D faces.
    Array2D = 2  ///< 1 to [[asset::kMaxTextureLayers]] 2D images of one size.
};

/// How a target API wants the texels laid out for upload.
///
/// - Thread-safety: immutable value.
struct TextureLayout
{
    u32 row_alignment = 4;           ///< Rows start a multiple of this many bytes apart (a power of two).
    u32 subresource_alignment = 16;  ///< Each subresource starts at a multiple of this (a power of two).
};

/// Returns the layout of target `target` (0 development and Vulkan: rows 4, subresources 16; 1 Direct3D 12: 256 and
/// 512; 2 Metal: 16 and 16).
///
/// - Param layout: set on success.
/// - Fails: `Invalid` for a target tag this version does not know.
/// - Ownership: `*layout` is written.
Status TextureLayoutForTarget(u32 target, TextureLayout* layout);

/// What the minter hands to [[asset::BuildTextureBlob]]: the mip chain of every layer, rows tightly packed.
///
/// - Thread-safety: thread-compatible.
/// - Ownership: borrows the spans for the call.
struct TextureBuildInput
{
    TextureKind kind = TextureKind::Tex2D;        ///< The kind.
    rhi::Format format = rhi::Format::Undefined;  ///< A format [[asset::IsTextureFormat]] accepts.
    u32 width = 0;                                ///< Level 0 width (a cube's faces are square).
    u32 height = 0;                               ///< Level 0 height.
    u32 layers = 1;                               ///< 1 for 2D, 6 for a cube, 1 to 2048 for an array.
    u32 mip_count = 1;                            ///< 1 to the full chain length.
    Span<const ByteSpan> subresources;            ///< `layers * mip_count` levels, layer-major, tightly packed rows.
    MipFilter filter = MipFilter::Box;            ///< What the minter used (recorded, not applied).
    MipAlpha alpha = MipAlpha::Straight;  ///< What the minter used; `Premultiply` marks the texture premultiplied.
};

/// Builds a texture blob for target `target`: lays the subresources out for the target and writes the sections.
///
/// - Fails: `Invalid` (an unsupported format, a size or count out of range, a cube that is not square or has not six
///   layers, a 2D texture with other than one layer, a wrong number of subresources or one of the wrong size, an
///   unknown target); `LimitExceeded` when memory runs out; `TooLarge` above the container's bound.
/// - Ownership: borrows `input` for the call; `*out` is replaced.
Status BuildTextureBlob(const TypeRegistry& types, ResourceId id, const TextureBuildInput& input, Vector<byte>* out,
                        u32 target = 0);

/// One subresource of a texture blob read in place.
///
/// - Thread-safety: immutable value.
/// - Ownership: borrows the blob bytes.
struct TextureLevel
{
    ByteSpan bytes;     ///< `row_pitch * block rows` bytes (the last row is padded too).
    u32 row_pitch = 0;  ///< Bytes between rows, a multiple of the texel size.
    u32 width = 0;      ///< Texels across.
    u32 height = 0;     ///< Texels down.
};

/// A texture blob read in place.
///
/// - Thread-safety: thread-compatible; immutable once opened.
/// - Ownership: borrows the blob bytes, which must outlive the view.
class TextureView
{
public:
    /// Creates an empty view.
    TextureView() = default;

    /// Validates `data` as a texture blob of the current version and keeps spans into it.
    ///
    /// - Fails: the errors of [[asset::BlobReader::Open]]; `TypeMismatch` for another type; `Corrupt` for a size or
    ///   count out of range, a kind that does not match the layer count or shape, a format this version does not
    ///   know, alignments that are not powers of two, a table of the wrong size, a subresource whose pitch, size or
    ///   offset differs from what the header implies, or one that overlaps another or ends past the data.
    /// - Ownership: borrows `data` and `types` for the call; the view keeps spans into `data`.
    Status Open(ByteSpan data, const TypeRegistry& types);

    /// The resource id in the blob's header.
    ResourceId Id() const { return id_; }
    /// The kind.
    TextureKind Kind() const { return kind_; }
    /// The texel format.
    rhi::Format Format() const { return format_; }
    /// True for `RGBA8_SRGB` (color channels sRGB-encoded).
    ///
    /// - Fails: never.
    bool Srgb() const { return rhi::FormatInfo(format_).is_srgb; }
    /// True when the colors were multiplied by alpha in the minter.
    ///
    /// - Fails: never.
    bool Premultiplied() const { return alpha_ == MipAlpha::Premultiply; }
    /// Level 0 width in texels.
    u32 Width() const { return width_; }
    /// Level 0 height in texels.
    u32 Height() const { return height_; }
    /// The layers: 1, 6 or the array size.
    u32 Layers() const { return layers_; }
    /// The stored mip levels per layer.
    u32 MipCount() const { return mip_count_; }
    /// The filter the minter used.
    MipFilter Filter() const { return filter_; }
    /// The alpha treatment the minter used.
    MipAlpha Alpha() const { return alpha_; }
    /// The recorded layout.
    TextureLayout Layout() const { return layout_; }

    /// Subresource `mip` of layer `layer`.
    ///
    /// - Aborts: `layer` is not below `Layers()` or `mip` not below `MipCount()`.
    TextureLevel Level(u32 layer, u32 mip) const;

    /// The description of the texture to create for this blob: a 2D texture (a cube is a 6-layer array), the stored
    /// mip count, and `usage`.
    ///
    /// - Param usage: how the texture will be used (the upload adds the copy-destination flag).
    rhi::TextureDesc Desc(rhi::TextureUsage usage) const;

private:
    ResourceId id_;
    TextureKind kind_ = TextureKind::Tex2D;
    rhi::Format format_ = rhi::Format::Undefined;
    u32 width_ = 0;
    u32 height_ = 0;
    u32 layers_ = 0;
    u32 mip_count_ = 0;
    MipFilter filter_ = MipFilter::Box;
    MipAlpha alpha_ = MipAlpha::Straight;
    TextureLayout layout_;
    ByteSpan table_;
    ByteSpan data_;
};

/// Re-lays a texture blob out for another target: the same id, texels and recorded options, new pitches and offsets.
///
/// - Fails: the errors of [[asset::TextureView::Open]] and [[asset::BuildTextureBlob]].
/// - Ownership: borrows `blob` and `types` for the call; `*out` is replaced.
Status RetargetTextureBlob(const TypeRegistry& types, ByteSpan blob, u32 target, Vector<byte>* out);

}  // namespace vse::asset
