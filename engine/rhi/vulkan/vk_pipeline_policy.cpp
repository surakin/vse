#include "rhi/vulkan/vk_pipeline_policy.h"

namespace engine_placeholder::rhi::vulkan
{

VkPrimitiveTopology ToVkTopology(PrimitiveTopology topology)
{
    switch (topology)
    {
    case PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkCullModeFlags ToVkCullMode(CullMode mode)
{
    switch (mode)
    {
    case CullMode::None: return VK_CULL_MODE_NONE;
    case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

VkFrontFace ToVkFrontFace(FrontFace face)
{
    return face == FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

VkCompareOp ToVkCompareOp(CompareOp op)
{
    switch (op)
    {
    case CompareOp::Never: return VK_COMPARE_OP_NEVER;
    case CompareOp::Less: return VK_COMPARE_OP_LESS;
    case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
    case CompareOp::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
    case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}

VkBlendFactor ToVkBlendFactor(BlendFactor factor)
{
    switch (factor)
    {
    case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
    case BlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
    case BlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }
    return VK_BLEND_FACTOR_ONE;
}

VkBlendOp ToVkBlendOp(BlendOp op)
{
    switch (op)
    {
    case BlendOp::Add: return VK_BLEND_OP_ADD;
    case BlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
    case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case BlendOp::Min: return VK_BLEND_OP_MIN;
    case BlendOp::Max: return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

VkFormat ToVkVertexFormat(VertexFormat format)
{
    switch (format)
    {
    case VertexFormat::Float1: return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::UInt1: return VK_FORMAT_R32_UINT;
    case VertexFormat::UInt2: return VK_FORMAT_R32G32_UINT;
    case VertexFormat::UInt3: return VK_FORMAT_R32G32B32_UINT;
    case VertexFormat::UInt4: return VK_FORMAT_R32G32B32A32_UINT;
    case VertexFormat::UNorm8x4: return VK_FORMAT_R8G8B8A8_UNORM;
    case VertexFormat::Count: break;
    }
    return VK_FORMAT_UNDEFINED;
}

VkIndexType ToVkIndexType(IndexType type)
{
    return type == IndexType::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
}

VkShaderStageFlags ToVkShaderStages(ShaderStage stages)
{
    VkShaderStageFlags flags = 0;
    if ((static_cast<u32>(stages) & static_cast<u32>(ShaderStage::Vertex)) != 0)
    {
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if ((static_cast<u32>(stages) & static_cast<u32>(ShaderStage::Fragment)) != 0)
    {
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if ((static_cast<u32>(stages) & static_cast<u32>(ShaderStage::Compute)) != 0)
    {
        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return flags;
}

VkDescriptorType ToVkDescriptorType(BindingType type)
{
    switch (type)
    {
    case BindingType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case BindingType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case BindingType::SampledTexture: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case BindingType::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
    case BindingType::StorageTexture: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case BindingType::ReadOnlyStorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case BindingType::Count: break;
    }
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

namespace
{

u32 ReadLe32(const byte* p)
{
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

// Appends to out[*pos], keeping room for the terminator; returns false once full.
bool Append(char* out, usize capacity, usize* pos, const char* text)
{
    for (; *text != '\0'; ++text)
    {
        if (*pos + 1 >= capacity)
        {
            return false;
        }
        out[(*pos)++] = *text;
    }
    return true;
}

bool AppendHex(char* out, usize capacity, usize* pos, u64 value, u32 digits)
{
    static const char kHex[] = "0123456789abcdef";
    for (u32 i = 0; i < digits; ++i)
    {
        if (*pos + 1 >= capacity)
        {
            return false;
        }
        out[(*pos)++] = kHex[(value >> (4 * (digits - 1 - i))) & 0xfu];
    }
    return true;
}

}  // namespace

const char* CheckPipelineCacheHeader(const void* data, usize size, const PipelineCacheIdentity& identity)
{
    if (data == nullptr || size < kPipelineCacheHeaderBytes)
    {
        return "pipeline cache file is smaller than its header";
    }
    const byte* p = static_cast<const byte*>(data);
    if (ReadLe32(p) < kPipelineCacheHeaderBytes)
    {
        return "pipeline cache header size is too small";
    }
    if (ReadLe32(p + 4) != 1)
    {
        return "pipeline cache header version is not 1";
    }
    if (ReadLe32(p + 8) != identity.vendor_id)
    {
        return "pipeline cache is for a different vendor";
    }
    if (ReadLe32(p + 12) != identity.device_id)
    {
        return "pipeline cache is for a different device";
    }
    for (u32 i = 0; i < 16; ++i)
    {
        if (p[16 + i] != identity.uuid[i])
        {
            return "pipeline cache is from a different driver version";
        }
    }
    return nullptr;
}

bool BuildPipelineCachePath(const char* dir, const PipelineCacheIdentity& identity, char* out, usize capacity)
{
    if (capacity == 0)
    {
        return false;
    }
    out[0] = '\0';
    usize pos = 0;
    bool ok = Append(out, capacity, &pos, dir);
    if (ok && pos > 0 && out[pos - 1] != '/')
    {
        ok = Append(out, capacity, &pos, "/");
    }
    ok = ok && Append(out, capacity, &pos, "pipeline_cache_") &&
         AppendHex(out, capacity, &pos, identity.vendor_id, 4) && Append(out, capacity, &pos, "_") &&
         AppendHex(out, capacity, &pos, identity.device_id, 4) && Append(out, capacity, &pos, "_");
    for (u32 i = 0; ok && i < 16; ++i)
    {
        ok = AppendHex(out, capacity, &pos, identity.uuid[i], 2);
    }
    ok = ok && Append(out, capacity, &pos, ".bin");
    out[ok ? pos : 0] = '\0';
    return ok;
}

}  // namespace engine_placeholder::rhi::vulkan
