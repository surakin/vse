#include "rhi/dx12/dx12_format.h"

namespace engine_placeholder::rhi::dx12
{

DXGI_FORMAT ToDxgi(Format format)
{
    switch (format)
    {
    case Format::Undefined: return DXGI_FORMAT_UNKNOWN;
    case Format::R8_UNorm: return DXGI_FORMAT_R8_UNORM;
    case Format::RG8_UNorm: return DXGI_FORMAT_R8G8_UNORM;
    case Format::RGBA8_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case Format::BGRA8_UNorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case Format::BGRA8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case Format::R16_Float: return DXGI_FORMAT_R16_FLOAT;
    case Format::RG16_Float: return DXGI_FORMAT_R16G16_FLOAT;
    case Format::RGBA16_Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case Format::R32_Float: return DXGI_FORMAT_R32_FLOAT;
    case Format::RG32_Float: return DXGI_FORMAT_R32G32_FLOAT;
    case Format::RGB32_Float: return DXGI_FORMAT_R32G32B32_FLOAT;
    case Format::RGBA32_Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case Format::R32_UInt: return DXGI_FORMAT_R32_UINT;
    case Format::D32_Float: return DXGI_FORMAT_D32_FLOAT;
    case Format::D24_UNorm_S8_UInt: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case Format::D32_Float_S8_UInt: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    case Format::BC1_UNorm: return DXGI_FORMAT_BC1_UNORM;
    case Format::BC1_SRGB: return DXGI_FORMAT_BC1_UNORM_SRGB;
    case Format::BC3_UNorm: return DXGI_FORMAT_BC3_UNORM;
    case Format::BC3_SRGB: return DXGI_FORMAT_BC3_UNORM_SRGB;
    case Format::BC5_UNorm: return DXGI_FORMAT_BC5_UNORM;
    case Format::BC7_UNorm: return DXGI_FORMAT_BC7_UNORM;
    case Format::BC7_SRGB: return DXGI_FORMAT_BC7_UNORM_SRGB;
    case Format::Count: break;
    }
    return DXGI_FORMAT_UNKNOWN;
}

DXGI_FORMAT ToDxgiResource(Format format, TextureUsage usage)
{
    if (!Has(usage, TextureUsage::Sampled))
    {
        return ToDxgi(format);
    }
    switch (format)
    {
    case Format::D32_Float: return DXGI_FORMAT_R32_TYPELESS;
    case Format::D24_UNorm_S8_UInt: return DXGI_FORMAT_R24G8_TYPELESS;
    case Format::D32_Float_S8_UInt: return DXGI_FORMAT_R32G8X24_TYPELESS;
    default: return ToDxgi(format);
    }
}

DXGI_FORMAT ToDxgiShaderView(Format format)
{
    switch (format)
    {
    case Format::D32_Float: return DXGI_FORMAT_R32_FLOAT;
    case Format::D24_UNorm_S8_UInt: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case Format::D32_Float_S8_UInt: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default: return ToDxgi(format);
    }
}

DXGI_FORMAT ToDxgiDepthView(Format format)
{
    return FormatInfo(format).is_depth ? ToDxgi(format) : DXGI_FORMAT_UNKNOWN;
}

DXGI_FORMAT ToDxgiVertex(VertexFormat format)
{
    switch (format)
    {
    case VertexFormat::Float1: return DXGI_FORMAT_R32_FLOAT;
    case VertexFormat::Float2: return DXGI_FORMAT_R32G32_FLOAT;
    case VertexFormat::Float3: return DXGI_FORMAT_R32G32B32_FLOAT;
    case VertexFormat::Float4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case VertexFormat::UInt1: return DXGI_FORMAT_R32_UINT;
    case VertexFormat::UInt2: return DXGI_FORMAT_R32G32_UINT;
    case VertexFormat::UInt3: return DXGI_FORMAT_R32G32B32_UINT;
    case VertexFormat::UInt4: return DXGI_FORMAT_R32G32B32A32_UINT;
    case VertexFormat::UNorm8x4: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case VertexFormat::Count: break;
    }
    return DXGI_FORMAT_UNKNOWN;
}

D3D12_RESOURCE_STATES ToD3D12State(ResourceState state)
{
    switch (state)
    {
    case ResourceState::Undefined: return D3D12_RESOURCE_STATE_COMMON;
    case ResourceState::VertexBuffer:
    case ResourceState::UniformBuffer: return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case ResourceState::IndexBuffer: return D3D12_RESOURCE_STATE_INDEX_BUFFER;
    case ResourceState::ShaderRead:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case ResourceState::ShaderWrite: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case ResourceState::CopySrc: return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case ResourceState::CopyDst: return D3D12_RESOURCE_STATE_COPY_DEST;
    case ResourceState::ColorAttachment: return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case ResourceState::DepthAttachment: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case ResourceState::DepthRead:
        return D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case ResourceState::Present: return D3D12_RESOURCE_STATE_PRESENT;
    case ResourceState::Count: break;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

}  // namespace engine_placeholder::rhi::dx12
