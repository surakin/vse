#include "rhi/vulkan/vk_format.h"

#include "log/assert.h"

namespace vse::rhi::vulkan
{

namespace
{

struct FormatPair
{
    Format format;
    VkFormat vk;
};

// One row per portable format except Undefined; the test checks every one is present and consistent.
constexpr FormatPair kFormats[] = {
    {Format::R8_UNorm, VK_FORMAT_R8_UNORM},
    {Format::RG8_UNorm, VK_FORMAT_R8G8_UNORM},
    {Format::RGBA8_UNorm, VK_FORMAT_R8G8B8A8_UNORM},
    {Format::RGBA8_SRGB, VK_FORMAT_R8G8B8A8_SRGB},
    {Format::BGRA8_UNorm, VK_FORMAT_B8G8R8A8_UNORM},
    {Format::BGRA8_SRGB, VK_FORMAT_B8G8R8A8_SRGB},
    {Format::R16_Float, VK_FORMAT_R16_SFLOAT},
    {Format::RG16_Float, VK_FORMAT_R16G16_SFLOAT},
    {Format::RGBA16_Float, VK_FORMAT_R16G16B16A16_SFLOAT},
    {Format::R32_Float, VK_FORMAT_R32_SFLOAT},
    {Format::RG32_Float, VK_FORMAT_R32G32_SFLOAT},
    {Format::RGB32_Float, VK_FORMAT_R32G32B32_SFLOAT},
    {Format::RGBA32_Float, VK_FORMAT_R32G32B32A32_SFLOAT},
    {Format::R32_UInt, VK_FORMAT_R32_UINT},
    {Format::D32_Float, VK_FORMAT_D32_SFLOAT},
    {Format::D24_UNorm_S8_UInt, VK_FORMAT_D24_UNORM_S8_UINT},
    {Format::D32_Float_S8_UInt, VK_FORMAT_D32_SFLOAT_S8_UINT},
    {Format::BC1_UNorm, VK_FORMAT_BC1_RGBA_UNORM_BLOCK},
    {Format::BC1_SRGB, VK_FORMAT_BC1_RGBA_SRGB_BLOCK},
    {Format::BC3_UNorm, VK_FORMAT_BC3_UNORM_BLOCK},
    {Format::BC3_SRGB, VK_FORMAT_BC3_SRGB_BLOCK},
    {Format::BC5_UNorm, VK_FORMAT_BC5_UNORM_BLOCK},
    {Format::BC7_UNorm, VK_FORMAT_BC7_UNORM_BLOCK},
    {Format::BC7_SRGB, VK_FORMAT_BC7_SRGB_BLOCK},
    {Format::BC4_UNorm, VK_FORMAT_BC4_UNORM_BLOCK},
    {Format::R16_UNorm, VK_FORMAT_R16_UNORM},
};

}  // namespace

// The table has one row per format after Undefined, in enum order, so a format indexes it directly.
static_assert(sizeof(kFormats) / sizeof(kFormats[0]) == static_cast<usize>(Format::Count) - 1,
              "vk_format.cpp: the table must have one row for every Format except Undefined");

VkFormat ToVkFormat(Format format)
{
    const usize index = static_cast<usize>(format);
    if (index == 0 || index >= static_cast<usize>(Format::Count))
    {
        return VK_FORMAT_UNDEFINED;
    }
    ENGINE_ASSERT(kFormats[index - 1].format == format, "vk_format.cpp: table rows are out of enum order");
    return kFormats[index - 1].vk;
}

Format FromVkFormat(VkFormat format)
{
    for (const FormatPair& pair : kFormats)
    {
        if (pair.vk == format)
        {
            return pair.format;
        }
    }
    return Format::Undefined;
}

VkImageAspectFlags AspectsOf(Format format)
{
    const FormatProperties& props = FormatInfo(format);
    if (!props.is_depth)
    {
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
    return props.has_stencil ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_DEPTH_BIT;
}

VkImageAspectFlags CopyAspectOf(Format format)
{
    const FormatProperties& props = FormatInfo(format);
    if (!props.is_depth)
    {
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
    return props.has_stencil ? 0 : VK_IMAGE_ASPECT_DEPTH_BIT;
}

}  // namespace vse::rhi::vulkan
