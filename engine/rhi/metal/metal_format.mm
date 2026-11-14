// RHI formats on Metal (Phase 21 Day 300; metal_format.h).
#include "rhi/metal/metal_format.h"

#include "log/assert.h"

namespace engine_placeholder::rhi::metal
{

MTLPixelFormat ToMtlPixelFormat(Format format, id<MTLDevice> device)
{
    ENGINE_ASSERT(device != nil, "ToMtlPixelFormat: the answer depends on the device; there is none");
    const bool bc = device.supportsBCTextureCompression;
    switch (format)
    {
    case Format::R8_UNorm: return MTLPixelFormatR8Unorm;
    case Format::RG8_UNorm: return MTLPixelFormatRG8Unorm;
    case Format::RGBA8_UNorm: return MTLPixelFormatRGBA8Unorm;
    case Format::RGBA8_SRGB: return MTLPixelFormatRGBA8Unorm_sRGB;
    case Format::BGRA8_UNorm: return MTLPixelFormatBGRA8Unorm;
    case Format::BGRA8_SRGB: return MTLPixelFormatBGRA8Unorm_sRGB;
    case Format::R16_Float: return MTLPixelFormatR16Float;
    case Format::RG16_Float: return MTLPixelFormatRG16Float;
    case Format::RGBA16_Float: return MTLPixelFormatRGBA16Float;
    case Format::R32_Float: return MTLPixelFormatR32Float;
    case Format::RG32_Float: return MTLPixelFormatRG32Float;
    case Format::RGBA32_Float: return MTLPixelFormatRGBA32Float;
    case Format::R32_UInt: return MTLPixelFormatR32Uint;
    case Format::D32_Float: return MTLPixelFormatDepth32Float;
    case Format::D24_UNorm_S8_UInt:
        return device.depth24Stencil8PixelFormatSupported ? MTLPixelFormatDepth24Unorm_Stencil8 : MTLPixelFormatInvalid;
    case Format::D32_Float_S8_UInt: return MTLPixelFormatDepth32Float_Stencil8;
    case Format::BC1_UNorm: return bc ? MTLPixelFormatBC1_RGBA : MTLPixelFormatInvalid;
    case Format::BC1_SRGB: return bc ? MTLPixelFormatBC1_RGBA_sRGB : MTLPixelFormatInvalid;
    case Format::BC3_UNorm: return bc ? MTLPixelFormatBC3_RGBA : MTLPixelFormatInvalid;
    case Format::BC3_SRGB: return bc ? MTLPixelFormatBC3_RGBA_sRGB : MTLPixelFormatInvalid;
    case Format::BC5_UNorm: return bc ? MTLPixelFormatBC5_RGUnorm : MTLPixelFormatInvalid;
    case Format::BC7_UNorm: return bc ? MTLPixelFormatBC7_RGBAUnorm : MTLPixelFormatInvalid;
    case Format::BC7_SRGB: return bc ? MTLPixelFormatBC7_RGBAUnorm_sRGB : MTLPixelFormatInvalid;
    case Format::BC4_UNorm: return bc ? MTLPixelFormatBC4_RUnorm : MTLPixelFormatInvalid;
    case Format::RGB32_Float:  // vertex data only: Metal has no three-channel 32-bit texture format
    case Format::Undefined:
    case Format::Count: break;
    }
    return MTLPixelFormatInvalid;
}

bool MtlFormatSupports(Format format, TextureUsage usage, id<MTLDevice> device)
{
    if (ToMtlPixelFormat(format, device) == MTLPixelFormatInvalid || usage == TextureUsage::None)
    {
        return false;
    }
    const bool depth = format == Format::D32_Float || format == Format::D24_UNorm_S8_UInt ||
                       format == Format::D32_Float_S8_UInt;
    const bool compressed = (format >= Format::BC1_UNorm && format <= Format::BC7_SRGB) || format == Format::BC4_UNorm;
    const bool srgb = format == Format::RGBA8_SRGB || format == Format::BGRA8_SRGB;
    if (Has(usage, TextureUsage::DepthAttachment) && !depth)
    {
        return false;
    }
    if (Has(usage, TextureUsage::ColorAttachment) && (depth || compressed))
    {
        return false;
    }
    if (Has(usage, TextureUsage::Storage) && (depth || compressed || srgb || format == Format::BGRA8_UNorm))
    {
        return false;  // Mac2: no shader writes to sRGB, depth, compressed or BGRA formats
    }
    return true;  // sampling and copies: every texture format above
}

bool MtlFormatCanGenerateMips(Format format, id<MTLDevice> device)
{
    // generateMipmapsForTexture: needs a color-renderable, filterable format: no depth, compressed or integer format,
    // and 32-bit floats only where the GPU filters them.
    if (!MtlFormatSupports(format, TextureUsage::Sampled | TextureUsage::ColorAttachment, device) ||
        format == Format::R32_UInt)
    {
        return false;
    }
    const bool float32 = format == Format::R32_Float || format == Format::RG32_Float || format == Format::RGBA32_Float;
    return !float32 || device.supports32BitFloatFiltering;
}

}  // namespace engine_placeholder::rhi::metal
