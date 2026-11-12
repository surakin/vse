#pragma once

#include "log/assert.h"
#include "types.h"

//! Texel and vertex-independent pixel formats, and a table describing their layout.
//!
//! The set is the intersection of what Vulkan, Direct3D 12 and Metal can all express. Not every format
//! is supported by every device or for every usage; ask the device.

namespace engine_placeholder::rhi
{

/// A texture or render-target pixel format.
///
/// `Undefined` is the zero value and is never valid for a resource. Names read as channel order, bit
/// width and numeric type, in the order they are stored in memory.
enum class Format : u8
{
    Undefined = 0,      ///< No format; invalid for a resource.
    R8_UNorm,           ///< One 8-bit normalized channel.
    RG8_UNorm,          ///< Two 8-bit normalized channels.
    RGBA8_UNorm,        ///< Four 8-bit normalized channels, linear.
    RGBA8_SRGB,         ///< Four 8-bit channels, sRGB-encoded color, linear alpha.
    BGRA8_UNorm,        ///< Four 8-bit normalized channels in blue-green-red-alpha order, linear.
    BGRA8_SRGB,         ///< Blue-green-red-alpha order, sRGB-encoded color; the usual swapchain format.
    R16_Float,          ///< One 16-bit float channel.
    RG16_Float,         ///< Two 16-bit float channels.
    RGBA16_Float,       ///< Four 16-bit float channels.
    R32_Float,          ///< One 32-bit float channel.
    RG32_Float,         ///< Two 32-bit float channels.
    RGB32_Float,        ///< Three 32-bit float channels; usually valid for vertex data only.
    RGBA32_Float,       ///< Four 32-bit float channels.
    R32_UInt,           ///< One 32-bit unsigned integer channel.
    D32_Float,          ///< 32-bit float depth.
    D24_UNorm_S8_UInt,  ///< 24-bit normalized depth and 8-bit stencil.
    D32_Float_S8_UInt,  ///< 32-bit float depth and 8-bit stencil.
    BC1_UNorm,          ///< Block-compressed RGB(A), 4x4 blocks of 8 bytes, linear. Reserved for the asset pipeline.
    BC1_SRGB,           ///< BC1 with sRGB-encoded color. Reserved.
    BC3_UNorm,          ///< Block-compressed RGBA, 4x4 blocks of 16 bytes, linear. Reserved.
    BC3_SRGB,           ///< BC3 with sRGB-encoded color. Reserved.
    BC5_UNorm,          ///< Block-compressed two-channel (normal maps), 4x4 blocks of 16 bytes. Reserved.
    BC7_UNorm,          ///< Block-compressed high-quality RGBA, 4x4 blocks of 16 bytes, linear. Reserved.
    BC7_SRGB,           ///< BC7 with sRGB-encoded color. Reserved.
    Count,              ///< Number of formats including `Undefined`; not a format.
};

/// Static layout facts about one [[rhi::Format]].
///
/// - Thread-safety: immutable. Values come from a constant table.
struct FormatProperties
{
    /// Lowercase printable name, for logs and tests.
    const char* name;
    /// Bytes per texel, or per block for compressed formats.
    ///
    /// Nominal for combined depth-stencil formats, whose memory layout is implementation-defined and which
    /// cannot be used with buffer copies.
    u8 bytes_per_block;
    /// Width in texels of one block; 1 for uncompressed formats.
    u8 block_width;
    /// Height in texels of one block; 1 for uncompressed formats.
    u8 block_height;
    /// Number of color/depth channels (stencil counts as one).
    u8 channels;
    /// Whether the format has a depth aspect.
    bool is_depth;
    /// Whether the format has a stencil aspect.
    bool has_stencil;
    /// Whether color channels are sRGB-encoded.
    bool is_srgb;
    /// Whether the format is block-compressed.
    bool is_compressed;
};

namespace detail
{
inline constexpr FormatProperties kFormatTable[static_cast<usize>(Format::Count)] = {
    // name,                bytes, bw, bh, ch, depth, stencil, srgb, compressed
    {"undefined", 0, 1, 1, 0, false, false, false, false},
    {"r8_unorm", 1, 1, 1, 1, false, false, false, false},
    {"rg8_unorm", 2, 1, 1, 2, false, false, false, false},
    {"rgba8_unorm", 4, 1, 1, 4, false, false, false, false},
    {"rgba8_srgb", 4, 1, 1, 4, false, false, true, false},
    {"bgra8_unorm", 4, 1, 1, 4, false, false, false, false},
    {"bgra8_srgb", 4, 1, 1, 4, false, false, true, false},
    {"r16_float", 2, 1, 1, 1, false, false, false, false},
    {"rg16_float", 4, 1, 1, 2, false, false, false, false},
    {"rgba16_float", 8, 1, 1, 4, false, false, false, false},
    {"r32_float", 4, 1, 1, 1, false, false, false, false},
    {"rg32_float", 8, 1, 1, 2, false, false, false, false},
    {"rgb32_float", 12, 1, 1, 3, false, false, false, false},
    {"rgba32_float", 16, 1, 1, 4, false, false, false, false},
    {"r32_uint", 4, 1, 1, 1, false, false, false, false},
    {"d32_float", 4, 1, 1, 1, true, false, false, false},
    {"d24_unorm_s8_uint", 4, 1, 1, 2, true, true, false, false},
    {"d32_float_s8_uint", 8, 1, 1, 2, true, true, false, false},
    {"bc1_unorm", 8, 4, 4, 4, false, false, false, true},
    {"bc1_srgb", 8, 4, 4, 4, false, false, true, true},
    {"bc3_unorm", 16, 4, 4, 4, false, false, false, true},
    {"bc3_srgb", 16, 4, 4, 4, false, false, true, true},
    {"bc5_unorm", 16, 4, 4, 2, false, false, false, true},
    {"bc7_unorm", 16, 4, 4, 4, false, false, false, true},
    {"bc7_srgb", 16, 4, 4, 4, false, false, true, true},
};
}  // namespace detail

/// Returns the layout facts for `format`.
///
/// - Param format: any value below `Format::Count`.
/// - Returns: a reference to a static table entry, valid for the life of the program.
/// - Aborts: `format` is `Format::Count` or larger.
inline const FormatProperties& FormatInfo(Format format)
{
    ENGINE_ASSERT(static_cast<usize>(format) < static_cast<usize>(Format::Count),
                  "rhi::FormatInfo: format out of range");
    return detail::kFormatTable[static_cast<usize>(format)];
}

/// Returns the extent of mip level `mip` of a dimension `base` texels long: halved per level, never below 1.
///
/// - Param base: the level-0 width, height or depth.
/// - Param mip: the mip level.
/// - Fails: never.
inline u32 MipExtent(u32 base, u32 mip)
{
    const u32 e = mip < 32 ? base >> mip : 0;
    return e == 0 ? 1 : e;
}

/// Returns the number of bytes in one row of `width` texels of `format`, rounding a partial block up.
///
/// - Param format: the texel format; must not be `Undefined`.
/// - Param width: row width in texels.
/// - Aborts: `format` is out of range.
inline u64 FormatRowBytes(Format format, u32 width)
{
    const FormatProperties& p = FormatInfo(format);
    u64 blocks = (static_cast<u64>(width) + p.block_width - 1) / p.block_width;
    return blocks * p.bytes_per_block;
}

/// Returns the number of bytes in one `width` by `height` image slice of `format`, rounding partial blocks up.
///
/// - Param format: the texel format; must not be `Undefined`.
/// - Param width: slice width in texels.
/// - Param height: slice height in texels.
/// - Aborts: `format` is out of range.
inline u64 FormatSliceBytes(Format format, u32 width, u32 height)
{
    const FormatProperties& p = FormatInfo(format);
    u64 block_rows = (static_cast<u64>(height) + p.block_height - 1) / p.block_height;
    return FormatRowBytes(format, width) * block_rows;
}

}  // namespace engine_placeholder::rhi
