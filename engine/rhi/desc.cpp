#include "rhi/desc.h"

#include "rhi/spirv.h"

namespace engine_placeholder::rhi
{

namespace
{

bool IsKnownFormat(Format f)
{
    return static_cast<usize>(f) < static_cast<usize>(Format::Count);
}

bool IsColorFormat(Format f)
{
    if (f == Format::Undefined || !IsKnownFormat(f))
    {
        return false;
    }
    const FormatProperties& p = FormatInfo(f);
    return !p.is_depth && !p.is_compressed;
}

// True when `v` is at most `last`; used to reject enum values that were cast from garbage.
template <typename E> bool AtMost(E v, E last)
{
    return static_cast<u32>(v) <= static_cast<u32>(last);
}

bool IsSingleStage(ShaderStage s)
{
    return s == ShaderStage::Vertex || s == ShaderStage::Fragment || s == ShaderStage::Compute;
}

// Layout list and push-constant checks shared by both pipeline kinds; the caller passes its own messages.
const char* ValidateLayouts(const BindGroupLayoutHandle* layouts, u32 count, u32 push_bytes, const char* too_many,
                            const char* invalid, const char* push_unaligned, const char* push_too_big)
{
    if (count > kMaxBindGroups)
    {
        return too_many;
    }
    for (u32 i = 0; i < count; ++i)
    {
        if (!layouts[i].IsValid())
        {
            return invalid;
        }
    }
    if (push_bytes % 4 != 0)
    {
        return push_unaligned;
    }
    if (push_bytes > kMaxPushConstantBytes)
    {
        return push_too_big;
    }
    return nullptr;
}

}  // namespace

u32 MaxMipLevels(u32 width, u32 height, u32 depth)
{
    if (width == 0 || height == 0 || depth == 0)
    {
        return 0;
    }
    u32 largest = width > height ? width : height;
    if (depth > largest)
    {
        largest = depth;
    }
    u32 levels = 1;
    while (largest > 1)
    {
        largest >>= 1;
        ++levels;
    }
    return levels;
}

const char* BufferDesc::Validate() const
{
    if (size == 0)
    {
        return "BufferDesc: size is zero";
    }
    if (!Any(usage))
    {
        return "BufferDesc: no usage flags";
    }
    const BufferUsage known = BufferUsage::Vertex | BufferUsage::Index | BufferUsage::Uniform | BufferUsage::Storage |
                              BufferUsage::CopySrc | BufferUsage::CopyDst | BufferUsage::Indirect;
    if (Any(static_cast<BufferUsage>(static_cast<u32>(usage) & ~static_cast<u32>(known))))
    {
        return "BufferDesc: unknown usage bits";
    }
    if (static_cast<u8>(location) > static_cast<u8>(MemoryLocation::GpuToCpu))
    {
        return "BufferDesc: unknown memory location";
    }
    if (location == MemoryLocation::GpuToCpu && usage != BufferUsage::CopyDst)
    {
        return "BufferDesc: GpuToCpu buffers may only be copy destinations";
    }
    return nullptr;
}

const char* TextureDesc::Validate() const
{
    if (dimension == TextureDimension::Cube)
    {
        return "TextureDesc: cube textures are not supported yet";
    }
    if (static_cast<u8>(dimension) > static_cast<u8>(TextureDimension::Cube))
    {
        return "TextureDesc: unknown dimension";
    }
    if (width == 0 || height == 0 || depth == 0)
    {
        return "TextureDesc: extent has a zero component";
    }
    if (dimension == TextureDimension::Tex2D && depth != 1)
    {
        return "TextureDesc: 2D textures must have depth 1";
    }
    if (array_layers == 0)
    {
        return "TextureDesc: array_layers is zero";
    }
    if (dimension == TextureDimension::Tex3D && array_layers != 1)
    {
        return "TextureDesc: 3D textures cannot be arrays";
    }
    if (format == Format::Undefined || static_cast<usize>(format) >= static_cast<usize>(Format::Count))
    {
        return "TextureDesc: format is undefined";
    }
    if (mip_levels == 0)
    {
        return "TextureDesc: mip_levels is zero";
    }
    if (mip_levels > MaxMipLevels(width, height, depth))
    {
        return "TextureDesc: mip_levels exceeds the full chain for the extent";
    }
    if (samples != 1)
    {
        return "TextureDesc: multisampling is not supported";
    }
    if (!Any(usage))
    {
        return "TextureDesc: no usage flags";
    }
    const TextureUsage known = TextureUsage::Sampled | TextureUsage::Storage | TextureUsage::ColorAttachment |
                               TextureUsage::DepthAttachment | TextureUsage::CopySrc | TextureUsage::CopyDst;
    if (Any(static_cast<TextureUsage>(static_cast<u32>(usage) & ~static_cast<u32>(known))))
    {
        return "TextureDesc: unknown usage bits";
    }
    const FormatProperties& p = FormatInfo(format);
    if (Has(usage, TextureUsage::DepthAttachment) && !p.is_depth)
    {
        return "TextureDesc: depth attachment requires a depth format";
    }
    if (Has(usage, TextureUsage::ColorAttachment) && !IsColorFormat(format))
    {
        return "TextureDesc: color attachment requires an uncompressed color format";
    }
    if (Has(usage, TextureUsage::Storage) && (p.is_depth || p.is_compressed))
    {
        return "TextureDesc: storage usage requires an uncompressed color format";
    }
    if (p.is_compressed && dimension == TextureDimension::Tex3D)
    {
        return "TextureDesc: compressed 3D textures are not supported";
    }
    return nullptr;
}

const char* SamplerDesc::Validate() const
{
    if (!(max_anisotropy >= 1.0f && max_anisotropy <= 16.0f))
    {
        return "SamplerDesc: max_anisotropy must be in [1, 16]";
    }
    if (!(min_lod >= 0.0f))
    {
        return "SamplerDesc: min_lod must be non-negative";
    }
    if (!(max_lod >= min_lod))
    {
        return "SamplerDesc: max_lod is below min_lod";
    }
    if (!AtMost(compare_op, CompareOp::Always))
    {
        return "SamplerDesc: unknown compare_op";
    }
    if (!AtMost(mag_filter, FilterMode::Linear) || !AtMost(min_filter, FilterMode::Linear) ||
        !AtMost(mip_filter, FilterMode::Linear))
    {
        return "SamplerDesc: unknown filter mode";
    }
    if (!AtMost(address_u, AddressMode::ClampToBorder) || !AtMost(address_v, AddressMode::ClampToBorder) ||
        !AtMost(address_w, AddressMode::ClampToBorder))
    {
        return "SamplerDesc: unknown address mode";
    }
    if (!AtMost(border_color, BorderColor::OpaqueWhite))
    {
        return "SamplerDesc: unknown border_color";
    }
    return nullptr;
}

const char* ShaderFormatName(ShaderFormat format)
{
    switch (format)
    {
    case ShaderFormat::Spirv: return "spirv";
    case ShaderFormat::Dxil: return "dxil";
    case ShaderFormat::MetalLib: return "metallib";
    }
    return "unknown";
}

const char* ShaderDesc::Validate() const
{
    if (!IsSingleStage(stage))
    {
        return "ShaderDesc: stage must be exactly one of vertex, fragment or compute";
    }
    if (bytes == nullptr || size == 0)
    {
        return "ShaderDesc: no bytecode";
    }
    switch (format)
    {
    case ShaderFormat::Spirv:
        if (reinterpret_cast<usize>(bytes) % alignof(u32) != 0)
        {
            return "ShaderDesc: SPIR-V bytecode is not 4-byte aligned";
        }
        if (size % sizeof(u32) != 0)
        {
            return "ShaderDesc: SPIR-V size is not a whole number of 32-bit words";
        }
        if (size < 5 * sizeof(u32))
        {
            return "ShaderDesc: module is smaller than a SPIR-V header";
        }
        break;
    case ShaderFormat::Dxil:
    {
        // A DXBC container: "DXBC", a 16-byte digest, a version (2 x u16), the total size (u32), the part count (u32).
        constexpr usize kHeader = 32;
        if (size < kHeader)
        {
            return "ShaderDesc: module is smaller than a DXIL container header";
        }
        const u8* b = static_cast<const u8*>(bytes);
        if (b[0] != 'D' || b[1] != 'X' || b[2] != 'B' || b[3] != 'C')
        {
            return "ShaderDesc: DXIL bytecode does not start with the DXBC container tag";
        }
        const u32 recorded = static_cast<u32>(b[24]) | (static_cast<u32>(b[25]) << 8) |
                             (static_cast<u32>(b[26]) << 16) | (static_cast<u32>(b[27]) << 24);
        if (recorded != size)
        {
            return "ShaderDesc: DXIL size differs from the size its container header records";
        }
        break;
    }
    case ShaderFormat::MetalLib:
    {
        // A Metal library: "MTLB", the target platform (u16), the file version (2 x u16), the library type (u8 at 10:
        // 0 executable, 2 dynamic; Phase 21 Day 282), the file size (u64 at 16), then the rest of the header. Only
        // executable libraries hold entry points a pipeline can use.
        constexpr usize kHeader = 32;
        if (size < kHeader)
        {
            return "ShaderDesc: module is smaller than a Metal library header";
        }
        const u8* b = static_cast<const u8*>(bytes);
        if (b[0] != 'M' || b[1] != 'T' || b[2] != 'L' || b[3] != 'B')
        {
            return "ShaderDesc: Metal library does not start with the MTLB tag";
        }
        if (b[10] != 0)
        {
            return "ShaderDesc: Metal library is not an executable library (a dynamic library has no usable entry points)";
        }
        u64 recorded = 0;
        for (u32 i = 0; i < 8; ++i)
        {
            recorded |= static_cast<u64>(b[16 + i]) << (8 * i);
        }
        if (recorded != size)
        {
            return "ShaderDesc: Metal library size differs from the size its header records";
        }
        break;
    }
    default: return "ShaderDesc: unknown bytecode format";
    }
    if (entry == nullptr || entry[0] == '\0')
    {
        return "ShaderDesc: entry point name is empty";
    }
    if (ShaderEntryNameTooLong(entry))
    {
        return "ShaderDesc: entry point name is longer than 63 bytes";
    }
    return nullptr;
}

const char* BindGroupLayoutDesc::Validate() const
{
    if (entries.Size() > kMaxBindingsPerLayout)
    {
        return "BindGroupLayoutDesc: too many bindings";
    }
    for (usize i = 0; i < entries.Size(); ++i)
    {
        const BindingDesc& e = entries[i];
        if (static_cast<usize>(e.type) >= static_cast<usize>(BindingType::Count))
        {
            return "BindGroupLayoutDesc: unknown binding type";
        }
        if (e.count == 0)
        {
            return "BindGroupLayoutDesc: binding count is zero";
        }
        if (e.count > kMaxBindingArraySize)
        {
            return "BindGroupLayoutDesc: binding array is too large";
        }
        if (!Any(e.stages) ||
            Any(static_cast<ShaderStage>(static_cast<u32>(e.stages) & ~static_cast<u32>(ShaderStage::All))))
        {
            return "BindGroupLayoutDesc: binding stages are empty or unknown";
        }
        const bool storage = e.type == BindingType::StorageBuffer || e.type == BindingType::ReadOnlyStorageBuffer;
        if (!storage && e.stride != 0)
        {
            return "BindGroupLayoutDesc: only a storage buffer binding has a stride";
        }
        if (e.stride > 2048)
        {
            return "BindGroupLayoutDesc: a storage buffer stride is larger than 2048 bytes";
        }
        if (e.type == BindingType::UniformBuffer || e.type == BindingType::StorageBuffer ||
            e.type == BindingType::ReadOnlyStorageBuffer || e.type == BindingType::StorageTexture)
        {
            // Arrays of buffers/storage images are not part of the Phase 4 contract.
            if (e.count != 1)
            {
                return "BindGroupLayoutDesc: only sampled textures and samplers can be arrays";
            }
        }
        for (usize j = 0; j < i; ++j)
        {
            if (entries[j].binding == e.binding)
            {
                return "BindGroupLayoutDesc: duplicate binding index";
            }
        }
    }
    return nullptr;
}

const char* BindGroupDesc::Validate() const
{
    if (!layout.IsValid())
    {
        return "BindGroupDesc: layout handle is invalid";
    }
    if (entries.Size() > usize{kMaxBindingsPerLayout} * 8)
    {
        return "BindGroupDesc: too many entries";
    }
    for (usize i = 0; i < entries.Size(); ++i)
    {
        for (usize j = 0; j < i; ++j)
        {
            if (entries[j].binding == entries[i].binding && entries[j].array_element == entries[i].array_element)
            {
                return "BindGroupDesc: duplicate entry";
            }
        }
    }
    return nullptr;
}

const char* GraphicsPipelineDesc::Validate() const
{
    if (!vertex_shader.IsValid())
    {
        return "GraphicsPipelineDesc: vertex shader is missing";
    }
    if (color_target_count > kMaxColorAttachments)
    {
        return "GraphicsPipelineDesc: too many color targets";
    }
    if (color_target_count > 0 && !fragment_shader.IsValid())
    {
        return "GraphicsPipelineDesc: color targets require a fragment shader";
    }
    if (color_target_count == 0 && depth_format == Format::Undefined)
    {
        return "GraphicsPipelineDesc: no attachments (no color targets and no depth format)";
    }
    for (u32 i = 0; i < color_target_count; ++i)
    {
        if (!IsColorFormat(color_targets[i].format))
        {
            return "GraphicsPipelineDesc: color target format is not an uncompressed color format";
        }
    }
    if (depth_format != Format::Undefined && (!IsKnownFormat(depth_format) || !FormatInfo(depth_format).is_depth))
    {
        return "GraphicsPipelineDesc: depth_format is not a depth format";
    }
    if ((depth_test || depth_write) && depth_format == Format::Undefined)
    {
        return "GraphicsPipelineDesc: depth state requires a depth format";
    }
    if (samples != 1)
    {
        return "GraphicsPipelineDesc: multisampling is not supported";
    }
    if (!AtMost(topology, PrimitiveTopology::PointList) || !AtMost(cull_mode, CullMode::Back) ||
        !AtMost(front_face, FrontFace::Clockwise) || !AtMost(depth_compare, CompareOp::Always))
    {
        return "GraphicsPipelineDesc: unknown fixed-function enum value";
    }
    if (primitive_restart && topology != PrimitiveTopology::TriangleStrip && topology != PrimitiveTopology::LineStrip)
    {
        return "GraphicsPipelineDesc: primitive_restart is only valid for the strip topologies";
    }
    for (u32 i = 0; i < color_target_count; ++i)
    {
        const ColorTargetDesc& t = color_targets[i];
        if (!AtMost(t.blend.src_color, BlendFactor::OneMinusDstAlpha) ||
            !AtMost(t.blend.dst_color, BlendFactor::OneMinusDstAlpha) ||
            !AtMost(t.blend.src_alpha, BlendFactor::OneMinusDstAlpha) ||
            !AtMost(t.blend.dst_alpha, BlendFactor::OneMinusDstAlpha) || !AtMost(t.blend.color_op, BlendOp::Max) ||
            !AtMost(t.blend.alpha_op, BlendOp::Max))
        {
            return "GraphicsPipelineDesc: unknown blend factor or operation";
        }
        if (static_cast<u32>(t.write_mask) > static_cast<u32>(ColorWriteMask::All))
        {
            return "GraphicsPipelineDesc: unknown color write mask bits";
        }
    }

    if (vertex_bindings.Size() > kMaxVertexBindings)
    {
        return "GraphicsPipelineDesc: too many vertex bindings";
    }
    if (vertex_attributes.Size() > kMaxVertexAttributes)
    {
        return "GraphicsPipelineDesc: too many vertex attributes";
    }
    for (usize i = 0; i < vertex_bindings.Size(); ++i)
    {
        if (vertex_bindings[i].stride == 0)
        {
            return "GraphicsPipelineDesc: vertex binding stride is zero";
        }
    }
    for (usize i = 0; i < vertex_attributes.Size(); ++i)
    {
        const VertexAttributeDesc& a = vertex_attributes[i];
        if (a.binding >= vertex_bindings.Size())
        {
            return "GraphicsPipelineDesc: vertex attribute references a missing binding";
        }
        u32 size = VertexFormatSize(a.format);
        if (size == 0)
        {
            return "GraphicsPipelineDesc: unknown vertex format";
        }
        if (static_cast<u64>(a.offset) + size > vertex_bindings[a.binding].stride)
        {
            return "GraphicsPipelineDesc: vertex attribute extends past its binding stride";
        }
        for (usize j = 0; j < i; ++j)
        {
            if (vertex_attributes[j].location == a.location)
            {
                return "GraphicsPipelineDesc: duplicate vertex attribute location";
            }
        }
    }

    const char* layout_error = ValidateLayouts(
        bind_group_layouts, bind_group_layout_count, push_constant_bytes,
        "GraphicsPipelineDesc: too many bind group layouts",
        "GraphicsPipelineDesc: bind group layout handle is invalid",
        "GraphicsPipelineDesc: push_constant_bytes is not a multiple of 4",
        "GraphicsPipelineDesc: push_constant_bytes exceeds the 128-byte guarantee");
    if (layout_error != nullptr)
    {
        return layout_error;
    }
    if (push_constant_bytes > 0 &&
        (!Any(push_constant_stages) || Any(static_cast<ShaderStage>(static_cast<u32>(push_constant_stages) &
                                                                    ~static_cast<u32>(ShaderStage::AllGraphics)))))
    {
        return "GraphicsPipelineDesc: push constants need graphics stages";
    }
    return nullptr;
}

const char* ComputePipelineDesc::Validate() const
{
    if (!shader.IsValid())
    {
        return "ComputePipelineDesc: compute shader is missing";
    }
    return ValidateLayouts(bind_group_layouts, bind_group_layout_count, push_constant_bytes,
                           "ComputePipelineDesc: too many bind group layouts",
                           "ComputePipelineDesc: bind group layout handle is invalid",
                           "ComputePipelineDesc: push_constant_bytes is not a multiple of 4",
                           "ComputePipelineDesc: push_constant_bytes exceeds the 128-byte guarantee");
}

}  // namespace engine_placeholder::rhi
