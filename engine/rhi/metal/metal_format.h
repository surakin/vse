#pragma once

// RHI formats as Metal pixel formats, and what each supports on a device (Phase 21 Days 300-301). Objective-C++.

#include "rhi/enums.h"
#include "rhi/format.h"

#import <Metal/Metal.h>

namespace engine_placeholder::rhi::metal
{

// The pixel format of `format`; MTLPixelFormatInvalid for one Metal has no texture format for (RGB32_Float is vertex
// data only) or this device lacks (Depth24Unorm_Stencil8 where depth24Stencil8PixelFormatSupported is false, BC without
// supportsBCTextureCompression). Aborts when `device` is nil: the answer depends on the device.
MTLPixelFormat ToMtlPixelFormat(Format format, id<MTLDevice> device);

// Whether a texture of `format` can be made with every usage in `usage` on `device` (Apple's feature-set tables for the
// Mac2 family: sRGB and depth formats are not shader-writable, depth formats are not color attachments, compressed
// formats are sampled and copied only).
bool MtlFormatSupports(Format format, TextureUsage usage, id<MTLDevice> device);

// Whether a blit can generate the mips of a texture of `format` on `device`: color-renderable and filterable (no depth,
// compressed or integer format; 32-bit floats only with supports32BitFloatFiltering), as Vulkan's linear-filter rule.
bool MtlFormatCanGenerateMips(Format format, id<MTLDevice> device);

}  // namespace engine_placeholder::rhi::metal
