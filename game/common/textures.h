#pragma once

// Procedural textures, the `.rtex` raw texture container and a helper that creates and fills a sampled texture. Shared
// by rhi_demo and the RHI tests; not part of the RHI.
//
// `.rtex` is a throwaway internal container (the asset pipeline brings the real formats): a 24-byte little-endian
// header - the magic "RTEX", a version (1), width, height, the `rhi::Format` value and the mip count - followed by
// every mip level's rows tightly packed, level 0 first. Only the 8-bit-per-channel RGBA and BGRA formats are supported
// (`RGBA8`/`BGRA8`, `UNorm` or `SRGB`).

#include "containers/span.h"
#include "containers/vector.h"
#include "io/file.h"
#include "rhi/rhi.h"

namespace demo
{

using namespace engine_placeholder;

struct Rgba
{
    byte r, g, b, a;
};

// An RGBA8 image, tightly packed, row 0 first.
struct Pixels
{
    u32 width = 0;
    u32 height = 0;
    Vector<byte> rgba;
};

// A `size` x `size` checkerboard of `cell`-texel squares; the square at the origin is `a`.
Pixels Checkerboard(u32 size, u32 cell, Rgba a, Rgba b);

// A `width` x `height` image filled with one color.
Pixels Solid(u32 width, u32 height, Rgba color);

// A `width` x `height` image whose red goes 0..255 along X and green 0..255 along Y.
Pixels UvGradient(u32 width, u32 height);

// The level count of a full mip chain.
u32 FullMipCount(u32 width, u32 height);

// Creates a `Sampled` texture of `format` (one of the formats above), uploads `pixels` as level 0 and, when
// `generate_mips` is set and the format allows it, fills the full mip chain on the GPU. It submits work and waits for
// it, so call it outside a frame (before the render loop). Returns an invalid handle, logging why, on failure.
rhi::TextureHandle CreateSampledTexture(rhi::Device* device, const Pixels& pixels, rhi::Format format,
                                        bool generate_mips, const char* debug_name);

// A texture loaded from or written to an `.rtex` file.
struct RtexImage
{
    u32 width = 0;
    u32 height = 0;
    rhi::Format format = rhi::Format::Undefined;
    u32 mip_count = 0;
    Vector<byte> data;  // every level, tightly packed
};

// Builds a full mip chain for `pixels` on the CPU (a 2x2 box filter; the color of an sRGB format is averaged as
// linear light, like the GPU's `GenerateMips`).
RtexImage BuildRtex(const Pixels& pixels, rhi::Format format);

// Writes `width` x `height` RGBA8 pixels as a binary PPM (the alpha channel is dropped).
io::Status WritePpm(const io::Path& path, u32 width, u32 height, const byte* rgba);

// Writes the image atomically (a failed write never leaves a half file). Fails with `Invalid` for a malformed image.
io::Status WriteRtex(const io::Path& path, const RtexImage& image);
// Reads and validates a file; `image` is unchanged on failure. A damaged, truncated or oversized file is `Invalid`; a
// missing one `NotFound`.
io::Status ReadRtex(const io::Path& path, RtexImage* image);

// Creates a `Sampled` texture from an `.rtex` image, uploading every stored level. Same rules as
// `CreateSampledTexture`.
rhi::TextureHandle CreateTextureFromRtex(rhi::Device* device, const RtexImage& image, const char* debug_name);

}  // namespace demo
