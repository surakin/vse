#include "renderer/material.h"

#include "cstr/cstr.h"
#include "lang/new.h"

#include "containers/unique_ptr.h"

#include "log/assert.h"
#include "renderer/frame_data.h"
#include "renderer/renderer_log.h"
#include "renderer/texture.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder::renderer
{

namespace
{

u64 MaterialAlignUp(u64 v, u64 a)
{
    return (v + a - 1) / a * a;
}

constexpr u32 kSlots = FrameResources::kSlots;
constexpr u32 kNameBytes = 48;

void CopyName(char (&dst)[kNameBytes], const char* src)
{
    u32 i = 0;
    if (src != nullptr)
    {
        for (; i + 1 < kNameBytes && src[i] != 0; ++i)
        {
            dst[i] = src[i];
        }
    }
    dst[i] = 0;
}

bool StartsWith(const char* s, const char* prefix)
{
    return CStrCompareN(s, prefix, CStrLength(prefix)) == 0;
}

DefaultTexture DefaultFor(const char* name)
{
    if (StartsWith(name, "normal"))
    {
        return DefaultTexture::FlatNormal;
    }
    if (StartsWith(name, "emissive") || StartsWith(name, "black"))
    {
        return DefaultTexture::Black;
    }
    return DefaultTexture::White;
}

bool SameBlock(const ReflectedBlock& a, const ReflectedBlock& b)
{
    if (a.size != b.size || a.member_count != b.member_count)
    {
        return false;
    }
    for (u32 i = 0; i < a.member_count; ++i)
    {
        const ReflectedMember& x = a.members[i];
        const ReflectedMember& y = b.members[i];
        if (CStrCompare(x.name, y.name) != 0 || x.offset != y.offset || x.size != y.size || x.kind != y.kind ||
            x.scalar != y.scalar || x.rows != y.rows || x.columns != y.columns || x.array_count != y.array_count)
        {
            return false;
        }
    }
    return true;
}

void SetMessage(ReflectionError* e, const char* fmt, const char* a, const char* b = "")
{
    if (e != nullptr)
    {
        FormatTo(e->message, sizeof(e->message), fmt, a, b);
    }
}

// The renderer's descriptor-set convention: set 0 one uniform buffer at binding 0, set 2 one storage buffer at binding
// 0, set 1 the material (uniform block, sampled textures, samplers), nothing else.
bool CheckConvention(const ShaderReflection& r, const char* what, ReflectionError* error)
{
    for (u32 i = 0; i < r.binding_count; ++i)
    {
        const ReflectedBinding& b = r.bindings[i];
        if (b.set > kObjectSet)
        {
            SetMessage(error, "%s: '%s' uses a descriptor set above 2", what, b.name);
            return false;
        }
        if (b.count != 1)
        {
            SetMessage(error, "%s: '%s' is a binding array, which materials do not support", what, b.name);
            return false;
        }
        if (b.set == kFrameSet && !(b.binding == 0 && b.type == rhi::BindingType::UniformBuffer))
        {
            SetMessage(error, "%s: set 0 must be a single uniform buffer at binding 0, but '%s' is not", what, b.name);
            return false;
        }
        if (b.set == kObjectSet && !(b.binding == 0 && b.type == rhi::BindingType::ReadOnlyStorageBuffer))
        {
            SetMessage(
                error,
                "%s: set 2 must be a single read-only storage buffer (StructuredBuffer) at binding 0, but '%s' is not",
                what, b.name);
            return false;
        }
        if (b.set == kMaterialSet && b.type != rhi::BindingType::UniformBuffer &&
            b.type != rhi::BindingType::SampledTexture && b.type != rhi::BindingType::Sampler)
        {
            SetMessage(error, "%s: set 1 holds a uniform block, textures and samplers only, but '%s' is something else",
                       what, b.name);
            return false;
        }
    }
    return true;
}

bool SameLayout(const VertexLayoutData& a, const VertexLayoutData& b)
{
    if (a.stride != b.stride || a.attribute_count != b.attribute_count)
    {
        return false;
    }
    for (u32 i = 0; i < a.attribute_count; ++i)
    {
        const rhi::VertexAttributeDesc& x = a.attributes[i];
        const rhi::VertexAttributeDesc& y = b.attributes[i];
        if (x.location != y.location || x.binding != y.binding || x.format != y.format || x.offset != y.offset)
        {
            return false;
        }
    }
    return true;
}

bool SameTarget(const PipelineTarget& a, const PipelineTarget& b)
{
    if (a.color_count != b.color_count || a.depth_format != b.depth_format)
    {
        return false;
    }
    for (u32 i = 0; i < a.color_count; ++i)
    {
        if (a.color_formats[i] != b.color_formats[i])
        {
            return false;
        }
    }
    return true;
}

}  // namespace

u64 MakeDrawSortKey(u32 pipeline_index, u32 material_index, u32 mesh_index)
{
    ENGINE_ASSERT(pipeline_index < (1u << 16) && material_index < (1u << 24) && mesh_index < (1u << 24),
                  "MakeDrawSortKey: an index is out of range");
    return (static_cast<u64>(pipeline_index) << 48) | (static_cast<u64>(material_index) << 24) | mesh_index;
}

// ---- data ----

struct MaterialSystem::Variant
{
    bool built = false;
    u32 key_bits = 0;
    rhi::ShaderHandle fragment;
    rhi::BindGroupLayoutHandle layout;
    bool has_block = false;
    u32 block_binding = 0;
    ReflectedBlock block;
    u32 texture_count = 0;
    struct TextureSlot
    {
        char name[kNameBytes] = {};
        u32 binding = 0;
        DefaultTexture fallback = DefaultTexture::White;
    } textures[kMaxMaterialTextures];
    u32 sampler_count = 0;
    struct SamplerSlot
    {
        char name[kNameBytes] = {};
        u32 binding = 0;
    } samplers[kMaxMaterialSamplers];
};

struct MaterialSystem::TemplateSlot
{
    bool alive = false;
    u32 generation = 1;
    char name[kNameBytes] = {};
    MaterialTemplateDesc desc;
    ShaderReflection vertex_reflection;
    rhi::ShaderHandle vertex;
    u32 variant_count = 0;
    Variant variants[kMaxPermutationKeys];
    u32 material_count = 0;
    u32 default_count = 0;
    struct SlotDefault
    {
        char name[kNameBytes] = {};
        DefaultTexture texture = DefaultTexture::White;
    } defaults[kMaxMaterialTextures];
};

struct MaterialSystem::MaterialSlot
{
    bool alive = false;
    u32 generation = 1;
    char name[kNameBytes] = {};
    u32 template_index = 0;
    u32 variant_index = 0;
    Vector<byte> shadow;
    u32 shadow_size = 0;
    rhi::BufferHandle params;
    byte* mapped = nullptr;
    u64 stride = 0;
    u32 dirty_slots = 0;
    rhi::TextureHandle textures[kMaxMaterialTextures];
    rhi::SamplerHandle samplers[kMaxMaterialSamplers];
    const GpuTexture* texture_owners[kMaxMaterialTextures] = {};  // when set, the slot follows the owner's handle
    const GpuSampler* sampler_owners[kMaxMaterialSamplers] = {};
    rhi::TextureHandle applied_textures[kMaxMaterialTextures];  // what the current bind groups were built from
    rhi::SamplerHandle applied_samplers[kMaxMaterialSamplers];
    rhi::BindGroupHandle groups[kSlots];
    bool groups_dirty = false;
    u32 rebuilds = 0;
};

struct MaterialSystem::PipelineEntry
{
    bool used = false;
    u32 template_index = 0;
    u32 template_generation = 0;
    u32 key_bits = 0;
    PipelineTarget target;
    VertexLayoutData layout;
    rhi::PrimitiveTopology topology = rhi::PrimitiveTopology::TriangleList;
    bool restart = false;
    rhi::PipelineHandle pipeline;
};

// ---- system ----

MaterialSystem::MaterialSystem(rhi::Device* device, const MaterialSystemDesc& desc) : device_(device), desc_(desc)
{
    ENGINE_ASSERT(device != nullptr, "MaterialSystem: the device is null");
    ENGINE_ASSERT(desc.max_templates > 0 && desc.max_materials > 0 && desc.max_pipelines > 0,
                  "MaterialSystem: a capacity is 0");
    ENGINE_ASSERT(desc.max_materials <= (1u << 24) && desc.max_pipelines <= (1u << 16),
                  "MaterialSystem: a capacity exceeds what the draw sort key can hold");
    templates_.Resize(desc.max_templates);
    materials_.Resize(desc.max_materials);
    pipelines_.Resize(desc.max_pipelines);

    const bool ok = CreateDefaults();
    valid_ = ok;
    if (!ok)
    {
        RenderLog(LogVerbosity::Error, "MaterialSystem: creating the default resources failed");
    }
    else if (!registration_.Attach(desc.resources, this, tiers::kMaterials))
    {
        RenderLog(LogVerbosity::Error,
                  "MaterialSystem: could not register with the device resources; it will not survive a reset");
    }
}

bool MaterialSystem::CreateDefaults()
{
    rhi::BindingDesc frame_binding;
    frame_binding.binding = 0;
    frame_binding.type = rhi::BindingType::UniformBuffer;
    frame_binding.stages = rhi::ShaderStage::AllGraphics;
    rhi::BindGroupLayoutDesc fl;
    fl.entries = rhi::Span<rhi::BindingDesc>(&frame_binding, 1);
    fl.debug_name = "frame layout";
    frame_layout_ = device_->CreateBindGroupLayout(fl);

    rhi::BindingDesc object_binding;
    object_binding.binding = 0;
    object_binding.type = rhi::BindingType::ReadOnlyStorageBuffer;
    object_binding.stride = sizeof(ObjectData);  // StructuredBuffer<ObjectData> objects (gbuffer.vert)
    object_binding.stages = rhi::ShaderStage::AllGraphics;
    rhi::BindGroupLayoutDesc ol;
    ol.entries = rhi::Span<rhi::BindingDesc>(&object_binding, 1);
    ol.debug_name = "object layout";
    object_layout_ = device_->CreateBindGroupLayout(ol);

    rhi::SamplerDesc sd;
    sd.debug_name = "default sampler";
    default_sampler_ = device_->CreateSampler(sd);

    const byte pixels[3][4] = {{255, 255, 255, 255}, {0, 0, 0, 255}, {128, 128, 255, 255}};
    const char* names[3] = {"default white", "default black", "default flat normal"};
    bool ok = frame_layout_.IsValid() && object_layout_.IsValid() && default_sampler_.IsValid();
    for (u32 i = 0; i < 3 && ok; ++i)
    {
        rhi::TextureDesc td;
        td.width = 1;
        td.height = 1;
        td.format = rhi::Format::RGBA8_UNorm;
        td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
        td.debug_name = names[i];
        defaults_[i] = device_->CreateTexture(td);
        if (!defaults_[i].IsValid() || !device_->UploadTexture(defaults_[i], 0, 0, pixels[i], 0).ok)
        {
            ok = false;
        }
    }
    if (ok && device_->Caps().object_index_stream)
    {
        rhi::BufferDesc bd;
        bd.size = u64{kMaxObjectIndices} * sizeof(u32);
        bd.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst;
        bd.debug_name = "object index stream";
        object_index_stream_ = device_->CreateBuffer(bd);
        Vector<u32> indices;
        indices.Resize(kMaxObjectIndices);
        for (u32 i = 0; i < kMaxObjectIndices; ++i)
        {
            indices[i] = i;
        }
        ok = object_index_stream_.IsValid() &&
             device_->UploadBuffer(object_index_stream_, 0, indices.Data(), bd.size).ok;
        if (ok)
        {
            // Into its read state once; nothing writes it again (the same one-shot submit as a mesh's, joining an open
            // frame).
            const bool own_frame = !device_->InFrame();
            if (own_frame)
            {
                device_->BeginFrame();
            }
            rhi::CommandList* list = device_->CreateCommandList();
            rhi::FenceValue fence = 0;
            if (list != nullptr)
            {
                list->Begin();
                list->Barrier(object_index_stream_, rhi::ResourceState::CopyDst, rhi::ResourceState::VertexBuffer);
                list->End();
                rhi::CommandList* lists[] = {list};
                fence = device_->Submit(lists, 1);
            }
            if (own_frame)
            {
                device_->EndFrame();
            }
            if (own_frame)
            {
                device_->WaitIdle();
            }
            else if (fence != 0)
            {
                device_->WaitFence(fence, ~0ull);
            }
            ok = fence != 0;
        }
    }
    if (!ok)
    {
        DestroyDefaults();
    }
    return ok;
}

bool MaterialSystem::AddObjectIndexAttribute(const rhi::Device& device, rhi::VertexBindingDesc* bindings,
                                             u32* binding_count, u32 binding_capacity,
                                             rhi::VertexAttributeDesc* attributes, u32* attribute_count,
                                             u32 attribute_capacity, u32 slot)
{
    if (!device.Caps().object_index_stream)
    {
        return true;
    }
    if (slot != *binding_count || *binding_count >= binding_capacity || *attribute_count >= attribute_capacity)
    {
        return false;
    }
    bindings[*binding_count] = rhi::VertexBindingDesc{sizeof(u32), true};
    ++*binding_count;
    rhi::VertexAttributeDesc a;
    a.location = kObjectIndexLocation;
    a.binding = slot;
    a.format = rhi::VertexFormat::UInt1;
    a.offset = 0;
    attributes[(*attribute_count)++] = a;
    return true;
}

void MaterialSystem::DestroyDefaults()
{
    for (rhi::TextureHandle& tex : defaults_)
    {
        if (tex.IsValid())
        {
            device_->DestroyTexture(tex);
        }
        tex = {};
    }
    if (default_sampler_.IsValid())
    {
        device_->DestroySampler(default_sampler_);
    }
    default_sampler_ = {};
    if (frame_layout_.IsValid())
    {
        device_->DestroyBindGroupLayout(frame_layout_);
    }
    frame_layout_ = {};
    if (object_layout_.IsValid())
    {
        device_->DestroyBindGroupLayout(object_layout_);
    }
    object_layout_ = {};
    if (object_index_stream_.IsValid())
    {
        device_->DestroyBuffer(object_index_stream_);
    }
    object_index_stream_ = {};
}

MaterialSystem::~MaterialSystem()
{
    registration_.Reset();
    Release();
}

void MaterialSystem::Release()
{
    if (released_ || device_ == nullptr)
    {
        return;
    }
    for (u32 i = 0; i < desc_.max_materials; ++i)
    {
        MaterialSlot& m = materials_[i];
        if (!m.alive)
        {
            continue;
        }
        for (rhi::BindGroupHandle& g : m.groups)
        {
            if (g.IsValid())
            {
                device_->DestroyBindGroup(g);
            }
            g = {};
        }
        if (m.mapped != nullptr)
        {
            device_->UnmapBuffer(m.params);
        }
        if (m.params.IsValid())
        {
            device_->DestroyBuffer(m.params);
        }
        m.mapped = nullptr;
        m.params = {};
        // Raw handles belong to the old device; owner bindings are kept and followed again after the rebuild.
        for (u32 k = 0; k < kMaxMaterialTextures; ++k)
        {
            if (m.texture_owners[k] == nullptr)
            {
                m.textures[k] = {};
            }
            m.applied_textures[k] = {};
        }
        for (u32 k = 0; k < kMaxMaterialSamplers; ++k)
        {
            if (m.sampler_owners[k] == nullptr)
            {
                m.samplers[k] = {};
            }
            m.applied_samplers[k] = {};
        }
        m.groups_dirty = true;
    }
    for (u32 i = 0; i < pipeline_count_; ++i)
    {
        PipelineEntry& p = pipelines_[i];
        if (p.used && p.pipeline.IsValid())
        {
            device_->DestroyPipeline(p.pipeline);
            p.pipeline = {};
            --stats_.pipelines;
        }
    }
    for (u32 i = 0; i < desc_.max_templates; ++i)
    {
        TemplateSlot& t = templates_[i];
        if (!t.alive)
        {
            continue;
        }
        for (u32 v = 0; v < t.variant_count; ++v)
        {
            if (t.variants[v].fragment.IsValid())
            {
                device_->DestroyShader(t.variants[v].fragment);
            }
            if (t.variants[v].layout.IsValid())
            {
                device_->DestroyBindGroupLayout(t.variants[v].layout);
            }
            t.variants[v].fragment = {};
            t.variants[v].layout = {};
        }
        if (t.vertex.IsValid())
        {
            device_->DestroyShader(t.vertex);
        }
        t.vertex = {};
    }
    for (u32 i = 0; i < frame_group_count_; ++i)
    {
        device_->DestroyBindGroup(frame_groups_[i].group);
    }
    for (u32 i = 0; i < object_group_count_; ++i)
    {
        device_->DestroyBindGroup(object_groups_[i].group);
    }
    frame_group_count_ = 0;
    object_group_count_ = 0;
    DestroyDefaults();
    released_ = true;
}

bool MaterialSystem::Recreate(rhi::Device* device)
{
    ENGINE_ASSERT(released_, "MaterialSystem::Recreate: the system still holds device objects");
    device_ = device;
    released_ = false;  // Release() below cleans up whatever was built if a step fails
    const u32 previous_rebuilds = stats_.rebuilds;
    auto fail = [&](const char* what)
    {
        RenderLog(LogVerbosity::Error, "MaterialSystem: recreating failed: %s", what);
        Release();
        stats_.rebuilds = previous_rebuilds;
        return false;
    };
    if (!CreateDefaults())
    {
        return fail("the default resources");
    }
    for (u32 i = 0; i < desc_.max_templates; ++i)
    {
        TemplateSlot& t = templates_[i];
        if (!t.alive)
        {
            continue;
        }
        rhi::ShaderDesc sd;
        sd.stage = rhi::ShaderStage::Vertex;
        sd.SetCode(t.desc.vertex.Blob(device_->Caps().shader_format));
        sd.debug_name = t.name;
        t.vertex = device_->CreateShader(sd);
        if (!t.vertex.IsValid())
        {
            return fail("a vertex shader");
        }
        // Rebuilt in the same order, so every material's variant index stays right.
        u32 keys[kMaxPermutationKeys];
        const u32 key_count = t.variant_count;
        for (u32 v = 0; v < key_count; ++v)
        {
            keys[v] = t.variants[v].key_bits;
        }
        t.variant_count = 0;
        for (u32 v = 0; v < key_count; ++v)
        {
            PermutationKey key;
            key.bits = keys[v];
            if (BuildVariant(t, key) == nullptr)
            {
                t.variant_count = key_count;  // entries not rebuilt yet still hold their CPU data; a retry starts from
                                              // them
                return fail("a material variant");
            }
        }
    }
    for (u32 i = 0; i < material_high_; ++i)
    {
        MaterialSlot& m = materials_[i];
        if (!m.alive)
        {
            continue;
        }
        if (!CreateParams(m))
        {
            return fail("a parameter buffer");
        }
        m.dirty_slots = m.shadow_size != 0 ? (1u << kSlots) - 1 : 0;
        for (u32 k = 0; k < kMaxMaterialTextures; ++k)
        {
            if (m.texture_owners[k] != nullptr)
            {
                m.textures[k] = m.texture_owners[k]->Handle();
            }
        }
        for (u32 k = 0; k < kMaxMaterialSamplers; ++k)
        {
            if (m.sampler_owners[k] != nullptr)
            {
                m.samplers[k] = m.sampler_owners[k]->Handle();
            }
        }
        if (!BuildGroups(m))
        {
            return fail("a material's bind groups");
        }
    }
    if (desc_.prewarm_pipelines)
    {
        for (u32 i = 0; i < pipeline_count_; ++i)
        {
            if (pipelines_[i].used && !RebuildPipeline(pipelines_[i]))
            {
                // Not fatal: the entry keeps its description and is built on first use, as in lazy mode.
                RenderLog(LogVerbosity::Warn,
                          "MaterialSystem: a pipeline could not be prewarmed; it will be built when first used");
            }
        }
    }
    ++stats_.rebuilds;
    return true;
}

bool MaterialSystem::Valid() const
{
    return valid_;
}

rhi::BindGroupLayoutHandle MaterialSystem::FrameLayout() const
{
    return frame_layout_;
}

rhi::BindGroupLayoutHandle MaterialSystem::ObjectLayout() const
{
    return object_layout_;
}

rhi::TextureHandle MaterialSystem::GetDefaultTexture(DefaultTexture kind) const
{
    return defaults_[static_cast<u32>(kind)];
}

MaterialStats MaterialSystem::Stats() const
{
    return stats_;
}

// ---- templates ----

TemplateId MaterialSystem::CreateTemplate(const MaterialTemplateDesc& desc)
{
    ENGINE_ASSERT(valid_, "MaterialSystem::CreateTemplate: the system is not valid");
    const char* name = desc.name != nullptr ? desc.name : "";
    u32 index = desc_.max_templates;
    for (u32 i = 0; i < desc_.max_templates; ++i)
    {
        if (!templates_[i].alive)
        {
            index = i;
            break;
        }
    }
    if (index == desc_.max_templates)
    {
        RenderLog(LogVerbosity::Error, "MaterialSystem: template '%s' not created: the template list is full", name);
        return TemplateId{};
    }
    if (desc.texture_default_count > kMaxMaterialTextures ||
        (desc.texture_default_count > 0 && desc.texture_defaults == nullptr))
    {
        RenderLog(LogVerbosity::Error,
                  "MaterialSystem: template '%s' not created: more texture defaults than texture slots", name);
        return TemplateId{};
    }
    if (!desc.vertex.HasCode() || desc.vertex.reflection == nullptr)
    {
        RenderLog(LogVerbosity::Error,
                  "MaterialSystem: template '%s' not created: the vertex module or its reflection is missing", name);
        return TemplateId{};
    }
    if (desc.fragment_permutations == nullptr && (!desc.fragment.HasCode() || desc.fragment.reflection == nullptr))
    {
        RenderLog(LogVerbosity::Error,
                  "MaterialSystem: template '%s' not created: the fragment module or its reflection is missing", name);
        return TemplateId{};
    }
    if (desc.fragment_permutations != nullptr)
    {
        if (const char* problem = ValidateShaderPermutations(*desc.fragment_permutations))
        {
            RenderLog(LogVerbosity::Error, "MaterialSystem: template '%s' not created: %s", name, problem);
            return TemplateId{};
        }
    }

    TemplateSlot& t = templates_[index];
    const char* problem = ParseShaderReflection(desc.vertex.reflection, desc.vertex.reflection_size,
                                                &t.vertex_reflection);
    ReflectionError error;
    if (problem != nullptr)
    {
        RenderLog(LogVerbosity::Error, "MaterialSystem: template '%s' not created: vertex reflection: %s", name,
                  problem);
        return TemplateId{};
    }
    if (t.vertex_reflection.stage != rhi::ShaderStage::Vertex)
    {
        RenderLog(
            LogVerbosity::Error,
            "MaterialSystem: template '%s' not created: the vertex module's reflection is not for a vertex shader",
            name);
        return TemplateId{};
    }
    if (!CheckConvention(t.vertex_reflection, "vertex shader", &error))
    {
        RenderLog(LogVerbosity::Error, "MaterialSystem: template '%s' not created: %s", name, error.message);
        return TemplateId{};
    }

    const rhi::ShaderFormat format = device_->Caps().shader_format;
    if (desc.vertex.Blob(format).Empty())
    {
        RenderLog(
            LogVerbosity::Error,
            "MaterialSystem: template '%s' not created: the vertex shader has no %s bytecode, which the %s device takes",
            name, rhi::ShaderFormatName(format), device_->BackendName());
        return TemplateId{};
    }
    rhi::ShaderDesc sd;
    sd.stage = rhi::ShaderStage::Vertex;
    sd.SetCode(desc.vertex.Blob(format));
    sd.debug_name = name;
    const rhi::ShaderHandle vertex = device_->CreateShader(sd);
    if (!vertex.IsValid())
    {
        RenderLog(LogVerbosity::Error,
                  "MaterialSystem: template '%s' not created: the vertex shader module was rejected", name);
        return TemplateId{};
    }

    t.vertex = vertex;
    t.default_count = desc.texture_default_count;
    for (u32 i = 0; i < t.default_count; ++i)
    {
        CopyName(t.defaults[i].name, desc.texture_defaults[i].slot);
        t.defaults[i].texture = desc.texture_defaults[i].texture;
    }
    t.desc = desc;
    t.desc.texture_defaults = nullptr;
    t.desc.texture_default_count = 0;
    CopyName(t.name, name);
    t.desc.name = t.name;
    t.variant_count = 0;
    t.material_count = 0;
    for (Variant& v : t.variants)
    {
        v = Variant{};
    }
    t.alive = true;
    TemplateId id;
    id.index = index;
    id.generation = t.generation;
    return id;
}

void MaterialSystem::DestroyTemplate(TemplateId id)
{
    ENGINE_ASSERT(id.IsValid() && id.index < desc_.max_templates && templates_[id.index].alive &&
                      templates_[id.index].generation == id.generation,
                  "MaterialSystem::DestroyTemplate: the id is invalid or stale");
    TemplateSlot& t = templates_[id.index];
    ENGINE_ASSERT(t.material_count == 0, "MaterialSystem::DestroyTemplate: materials still use the template");
    DestroyPipelines(id.index);
    for (u32 v = 0; v < t.variant_count; ++v)
    {
        if (t.variants[v].fragment.IsValid())
        {
            device_->DestroyShader(t.variants[v].fragment);
        }
        if (t.variants[v].layout.IsValid())
        {
            device_->DestroyBindGroupLayout(t.variants[v].layout);
        }
    }
    if (t.vertex.IsValid())
    {
        device_->DestroyShader(t.vertex);
    }
    t.vertex = {};
    t.variant_count = 0;
    t.alive = false;
    ++t.generation;
    if (t.generation == 0)
    {
        t.generation = 1;
    }
}

void MaterialSystem::DestroyPipelines(u32 template_index)
{
    for (u32 i = 0; i < pipeline_count_; ++i)
    {
        PipelineEntry& p = pipelines_[i];
        if (p.used && p.template_index == template_index)
        {
            if (p.pipeline.IsValid())
            {
                device_->DestroyPipeline(p.pipeline);
            }
            if (p.pipeline.IsValid())
            {
                --stats_.pipelines;
            }
            p = PipelineEntry{};
        }
    }
}

MaterialSystem::Variant* MaterialSystem::BuildVariant(TemplateSlot& t, PermutationKey key)
{
    const char* tname = t.name;
    const ShaderPermutations* table = t.desc.fragment_permutations;
    const u32 key_bits = table != nullptr ? key.bits : 0;
    for (u32 v = 0; v < t.variant_count; ++v)
    {
        if (t.variants[v].key_bits == key_bits)
        {
            return &t.variants[v];
        }
    }

    ReflectionError error;
    const PermutationModule* module = &t.desc.fragment;
    if (table != nullptr)
    {
        module = FindPermutation(*table, key, &error);
        if (module == nullptr)
        {
            RenderLog(LogVerbosity::Error, "MaterialSystem: material for template '%s': %s", tname, error.message);
            return nullptr;
        }
    }
    UniquePtr<ShaderReflection> fs_owner = MakeUnique<ShaderReflection>();
    if (!fs_owner)
    {
        RenderLog(LogVerbosity::Error, "MaterialSystem: out of memory");
        return nullptr;
    }
    ShaderReflection* fs = fs_owner.Get();
    if (const char* problem = ParseShaderReflection(module->reflection, module->reflection_size, fs))
    {
        RenderLog(LogVerbosity::Error, "MaterialSystem: template '%s': fragment reflection: %s", tname, problem);
        return nullptr;
    }
    if (fs->stage != rhi::ShaderStage::Fragment)
    {
        RenderLog(LogVerbosity::Error,
                  "MaterialSystem: template '%s': the fragment module's reflection is not for a fragment shader",
                  tname);
        return nullptr;
    }
    if (!CheckConvention(*fs, "fragment shader", &error))
    {
        RenderLog(LogVerbosity::Error, "MaterialSystem: template '%s': %s", tname, error.message);
        return nullptr;
    }

    Variant v;
    v.built = true;
    v.key_bits = key_bits;
    const ShaderReflection* stages[2] = {fs, &t.vertex_reflection};

    // The parameter block: at most one uniform binding in set 1, the same in both stages if both declare it.
    const ReflectedBlock* block = nullptr;
    u32 block_binding = 0;
    for (const ShaderReflection* r : stages)
    {
        for (u32 i = 0; i < r->binding_count; ++i)
        {
            const ReflectedBinding& b = r->bindings[i];
            if (b.set != kMaterialSet || b.type != rhi::BindingType::UniformBuffer)
            {
                continue;
            }
            if (b.block == kNoBlock || b.block >= r->block_count)
            {
                RenderLog(LogVerbosity::Error, "MaterialSystem: template '%s': uniform '%s' has no reflected block",
                          tname, b.name);
                return nullptr;
            }
            const ReflectedBlock* candidate = &r->blocks[b.block];
            if (block == nullptr)
            {
                block = candidate;
                block_binding = b.binding;
            }
            else if (b.binding != block_binding || !SameBlock(*block, *candidate))
            {
                RenderLog(
                    LogVerbosity::Error,
                    "MaterialSystem: template '%s': the shaders declare different parameter blocks in set 1 ('%s')",
                    tname, b.name);
                return nullptr;
            }
        }
    }
    if (block != nullptr)
    {
        if (block->size == 0 || block->size > kMaxMaterialParamBytes)
        {
            RenderLog(LogVerbosity::Error,
                      "MaterialSystem: template '%s': the parameter block is %u bytes; the limit is %u", tname,
                      block->size, kMaxMaterialParamBytes);
            return nullptr;
        }
        v.has_block = true;
        v.block_binding = block_binding;
        v.block = *block;
    }

    // Textures and samplers: distinct binding numbers, named by the first stage that declares them.
    for (const ShaderReflection* r : stages)
    {
        for (u32 i = 0; i < r->binding_count; ++i)
        {
            const ReflectedBinding& b = r->bindings[i];
            if (b.set != kMaterialSet)
            {
                continue;
            }
            if (b.type == rhi::BindingType::SampledTexture)
            {
                bool seen = false;
                for (u32 k = 0; k < v.texture_count; ++k)
                {
                    seen = seen || v.textures[k].binding == b.binding;
                }
                if (seen)
                {
                    continue;
                }
                if (v.texture_count == kMaxMaterialTextures)
                {
                    RenderLog(LogVerbosity::Error, "MaterialSystem: template '%s': more than %u textures", tname,
                              kMaxMaterialTextures);
                    return nullptr;
                }
                Variant::TextureSlot& s = v.textures[v.texture_count++];
                CopyName(s.name, b.name);
                s.binding = b.binding;
                s.fallback = DefaultFor(b.name);
                for (u32 d = 0; d < t.default_count; ++d)
                {
                    if (CStrCompare(t.defaults[d].name, b.name) == 0)
                    {
                        s.fallback = t.defaults[d].texture;
                    }
                }
            }
            else if (b.type == rhi::BindingType::Sampler)
            {
                bool seen = false;
                for (u32 k = 0; k < v.sampler_count; ++k)
                {
                    seen = seen || v.samplers[k].binding == b.binding;
                }
                if (seen)
                {
                    continue;
                }
                if (v.sampler_count == kMaxMaterialSamplers)
                {
                    RenderLog(LogVerbosity::Error, "MaterialSystem: template '%s': more than %u samplers", tname,
                              kMaxMaterialSamplers);
                    return nullptr;
                }
                Variant::SamplerSlot& s = v.samplers[v.sampler_count++];
                CopyName(s.name, b.name);
                s.binding = b.binding;
            }
        }
    }
    BindGroupLayoutData layout_data;
    if (!BuildBindGroupLayout(stages, 2, kMaterialSet, &layout_data, &error))
    {
        RenderLog(LogVerbosity::Error, "MaterialSystem: template '%s': set 1 does not merge: %s", tname, error.message);
        return nullptr;
    }
    const rhi::ShaderFormat format = device_->Caps().shader_format;
    if (module->Blob(format).Empty())
    {
        RenderLog(LogVerbosity::Error,
                  "MaterialSystem: template '%s': the fragment shader has no %s bytecode, which the %s device takes",
                  tname, rhi::ShaderFormatName(format), device_->BackendName());
        return nullptr;
    }
    v.layout = device_->CreateBindGroupLayout(layout_data.Desc(tname));
    rhi::ShaderDesc sd;
    sd.stage = rhi::ShaderStage::Fragment;
    sd.SetCode(module->Blob(format));
    sd.debug_name = tname;
    v.fragment = device_->CreateShader(sd);
    if (!v.layout.IsValid() || !v.fragment.IsValid())
    {
        RenderLog(LogVerbosity::Error, "MaterialSystem: template '%s': creating the layout or fragment shader failed",
                  tname);
        if (v.layout.IsValid())
        {
            device_->DestroyBindGroupLayout(v.layout);
        }
        if (v.fragment.IsValid())
        {
            device_->DestroyShader(v.fragment);
        }
        return nullptr;
    }
    t.variants[t.variant_count] = v;
    return &t.variants[t.variant_count++];
}

// ---- materials ----

const MaterialSystem::MaterialSlot& MaterialSystem::Get(MaterialId id) const
{
    ENGINE_ASSERT(id.IsValid() && id.index < desc_.max_materials && materials_[id.index].alive &&
                      materials_[id.index].generation == id.generation,
                  "MaterialSystem: the material id is invalid or stale");
    return materials_[id.index];
}

MaterialSystem::MaterialSlot& MaterialSystem::Get(MaterialId id)
{
    return const_cast<MaterialSlot&>(static_cast<const MaterialSystem*>(this)->Get(id));
}

bool MaterialSystem::IsAlive(MaterialId id) const
{
    return id.IsValid() && id.index < desc_.max_materials && materials_[id.index].alive &&
           materials_[id.index].generation == id.generation;
}

rhi::BindGroupHandle MaterialSystem::CachedBindGroup(CachedGroup* cache, u32* count, rhi::BindGroupLayoutHandle layout,
                                                     const BufferRegion& region)
{
    for (u32 i = 0; i < *count; ++i)
    {
        if (cache[i].buffer == region.buffer && cache[i].offset == region.offset && cache[i].size == region.size)
        {
            return cache[i].group;
        }
    }
    if (*count == kMaxCachedGroups)
    {
        // The oldest entry goes: a recreated FrameResources leaves groups for dead buffers behind, and the device
        // releases a destroyed group only after the frames that used it have finished.
        device_->DestroyBindGroup(cache[0].group);
        for (u32 i = 1; i < kMaxCachedGroups; ++i)
        {
            cache[i - 1] = cache[i];
        }
        --*count;
    }
    rhi::BindGroupEntry entry;
    entry.binding = 0;
    entry.buffer = region.buffer;
    entry.offset = region.offset;
    entry.size = region.size;
    rhi::BindGroupDesc bd;
    bd.layout = layout;
    bd.entries = rhi::Span<rhi::BindGroupEntry>(&entry, 1);
    const rhi::BindGroupHandle group = device_->CreateBindGroup(bd);
    if (!group.IsValid())
    {
        RenderLog(LogVerbosity::Error, "MaterialSystem: the device could not create a frame/object bind group");
        return rhi::BindGroupHandle{};
    }
    cache[*count] = CachedGroup{region.buffer, region.offset, region.size, group};
    ++*count;
    return group;
}

rhi::BindGroupHandle MaterialSystem::FrameBindGroup(const BufferRegion& region)
{
    return CachedBindGroup(frame_groups_, &frame_group_count_, frame_layout_, region);
}

rhi::BindGroupHandle MaterialSystem::ObjectBindGroup(const BufferRegion& region)
{
    return CachedBindGroup(object_groups_, &object_group_count_, object_layout_, region);
}

MaterialId MaterialSystem::MaterialAtIndex(u32 index) const
{
    if (index >= desc_.max_materials || !materials_[index].alive)
    {
        return MaterialId{};
    }
    return MaterialId{index, materials_[index].generation};
}

bool MaterialSystem::ReadsVertexInput(MaterialId id, u32 location) const
{
    const MaterialSlot& m = Get(id);
    const ShaderReflection& r = templates_[m.template_index].vertex_reflection;
    for (u32 i = 0; i < r.vertex_input_count; ++i)
    {
        if (r.vertex_inputs[i].location == location)
        {
            return true;
        }
    }
    return false;
}

MaterialId MaterialSystem::CreateMaterial(TemplateId tmpl, PermutationKey key, const char* name)
{
    ENGINE_ASSERT(tmpl.IsValid() && tmpl.index < desc_.max_templates && templates_[tmpl.index].alive &&
                      templates_[tmpl.index].generation == tmpl.generation,
                  "MaterialSystem::CreateMaterial: the template id is invalid or stale");
    TemplateSlot& t = templates_[tmpl.index];
    u32 index = desc_.max_materials;
    for (u32 i = 0; i < desc_.max_materials; ++i)
    {
        if (!materials_[i].alive)
        {
            index = i;
            break;
        }
    }
    if (index == desc_.max_materials)
    {
        RenderLog(LogVerbosity::Error, "MaterialSystem: material '%s' not created: the material list is full",
                  name != nullptr ? name : "");
        return MaterialId{};
    }
    Variant* variant = BuildVariant(t, key);
    if (variant == nullptr)
    {
        return MaterialId{};
    }

    MaterialSlot& m = materials_[index];
    const u32 generation = m.generation;
    m = MaterialSlot{};
    m.generation = generation;
    CopyName(m.name, name);
    m.template_index = tmpl.index;
    m.variant_index = static_cast<u32>(variant - t.variants);

    if (variant->has_block)
    {
        const u32 size = variant->block.size;
        const bool shadow_ok = m.shadow.TryResize(size);
        m.shadow_size = shadow_ok ? size : 0;
        m.stride = MaterialAlignUp(size, device_->Caps().min_uniform_buffer_offset_alignment);
        if (!shadow_ok || !CreateParams(m))
        {
            RenderLog(LogVerbosity::Error,
                      "MaterialSystem: material '%s' not created: its parameter buffer could not be made", m.name);
            DestroyMaterialObjects(m);
            return MaterialId{};
        }
        m.dirty_slots = (1u << kSlots) - 1;
    }
    if (!BuildGroups(m))
    {
        RenderLog(LogVerbosity::Error, "MaterialSystem: material '%s' not created: its bind groups could not be made",
                  m.name);
        DestroyMaterialObjects(m);
        return MaterialId{};
    }
    m.alive = true;
    if (index + 1 > material_high_)
    {
        material_high_ = index + 1;
    }
    ++t.material_count;
    MaterialId id;
    id.index = index;
    id.generation = m.generation;
    return id;
}

// Creates (or recreates) the mapped parameter buffer of a material whose size and stride are known.
bool MaterialSystem::CreateParams(MaterialSlot& m)
{
    const Variant& v = templates_[m.template_index].variants[m.variant_index];
    if (!v.has_block)
    {
        return true;
    }
    m.stride = MaterialAlignUp(v.block.size, device_->Caps().min_uniform_buffer_offset_alignment);
    rhi::BufferDesc bd;
    bd.size = m.stride * kSlots;
    bd.usage = rhi::BufferUsage::Uniform;
    bd.location = rhi::MemoryLocation::CpuToGpu;
    bd.debug_name = m.name;
    m.params = device_->CreateBuffer(bd);
    if (m.params.IsValid())
    {
        m.mapped = static_cast<byte*>(device_->MapBuffer(m.params));
    }
    return m.mapped != nullptr;
}

bool MaterialSystem::BuildGroups(MaterialSlot& m)
{
    const TemplateSlot& t = templates_[m.template_index];
    const Variant& v = t.variants[m.variant_index];
    rhi::BindGroupHandle groups[kSlots];
    for (u32 s = 0; s < kSlots; ++s)
    {
        rhi::BindGroupEntry entries[1 + kMaxMaterialTextures + kMaxMaterialSamplers];
        u32 n = 0;
        if (v.has_block)
        {
            rhi::BindGroupEntry& e = entries[n++];
            e.binding = v.block_binding;
            e.buffer = m.params;
            e.offset = m.stride * s;
            e.size = v.block.size;
        }
        for (u32 i = 0; i < v.texture_count; ++i)
        {
            rhi::BindGroupEntry& e = entries[n++];
            e.binding = v.textures[i].binding;
            e.texture = m.textures[i].IsValid() ? m.textures[i] : defaults_[static_cast<u32>(v.textures[i].fallback)];
        }
        for (u32 i = 0; i < v.sampler_count; ++i)
        {
            rhi::BindGroupEntry& e = entries[n++];
            e.binding = v.samplers[i].binding;
            e.sampler = m.samplers[i].IsValid() ? m.samplers[i] : default_sampler_;
        }
        rhi::BindGroupDesc gd;
        gd.layout = v.layout;
        gd.entries = rhi::Span<rhi::BindGroupEntry>(entries, n);
        gd.debug_name = m.name;
        groups[s] = device_->CreateBindGroup(gd);
        if (!groups[s].IsValid())
        {
            for (u32 k = 0; k < s; ++k)
            {
                device_->DestroyBindGroup(groups[k]);
            }
            return false;
        }
    }
    for (u32 s = 0; s < kSlots; ++s)
    {
        if (m.groups[s].IsValid())
        {
            device_->DestroyBindGroup(m.groups[s]);
        }
        m.groups[s] = groups[s];
    }
    for (u32 i = 0; i < kMaxMaterialTextures; ++i)
    {
        m.applied_textures[i] = m.textures[i];
    }
    for (u32 i = 0; i < kMaxMaterialSamplers; ++i)
    {
        m.applied_samplers[i] = m.samplers[i];
    }
    m.groups_dirty = false;
    return true;
}

void MaterialSystem::DestroyMaterialObjects(MaterialSlot& m)
{
    for (rhi::BindGroupHandle& g : m.groups)
    {
        if (g.IsValid())
        {
            device_->DestroyBindGroup(g);
        }
        g = {};
    }
    if (m.mapped != nullptr)
    {
        device_->UnmapBuffer(m.params);
    }
    if (m.params.IsValid())
    {
        device_->DestroyBuffer(m.params);
    }
    m.mapped = nullptr;
    m.params = {};
    m.shadow.Reset();
    m.alive = false;
}

void MaterialSystem::DestroyMaterial(MaterialId id)
{
    MaterialSlot& m = Get(id);
    --templates_[m.template_index].material_count;
    DestroyMaterialObjects(m);
    ++m.generation;
    if (m.generation == 0)
    {
        m.generation = 1;
    }
}

bool MaterialSystem::SetValue(MaterialId id, const char* name, MemberKind kind, ScalarType scalar, u32 rows,
                              u32 columns, const void* data, usize size)
{
    MaterialSlot& m = Get(id);
    const Variant& v = templates_[m.template_index].variants[m.variant_index];
    if (!v.has_block)
    {
        RenderLog(LogVerbosity::Error, "material '%s': has no parameter block, so '%s' cannot be set", m.name, name);
        return false;
    }
    const ReflectedMember* member = nullptr;
    for (u32 i = 0; i < v.block.member_count; ++i)
    {
        if (CStrCompare(v.block.members[i].name, name) == 0)
        {
            member = &v.block.members[i];
            break;
        }
    }
    if (member == nullptr)
    {
        RenderLog(LogVerbosity::Error, "material '%s': the parameter block has no member '%s'", m.name, name);
        return false;
    }
    if (member->array_count != 0 || member->kind != kind || member->scalar != scalar || member->rows != rows ||
        member->columns != columns || member->size != size || static_cast<u64>(member->offset) + size > m.shadow_size)
    {
        RenderLog(LogVerbosity::Error,
                  "material '%s': member '%s' is not of the type it is being set as (it is %u bytes, kind %u, %ux%u)",
                  m.name, name, member->size, static_cast<u32>(member->kind), static_cast<u32>(member->rows),
                  static_cast<u32>(member->columns));
        return false;
    }
    if (MemCompare(m.shadow.Data() + member->offset, data, size) != 0)
    {
        MemCopy(m.shadow.Data() + member->offset, data, size);
        m.dirty_slots = (1u << kSlots) - 1;
    }
    return true;
}

bool MaterialSystem::SetFloat(MaterialId id, const char* name, f32 value)
{
    return SetValue(id, name, MemberKind::Scalar, ScalarType::Float32, 1, 1, &value, sizeof(value));
}

bool MaterialSystem::SetInt(MaterialId id, const char* name, i32 value)
{
    return SetValue(id, name, MemberKind::Scalar, ScalarType::Int32, 1, 1, &value, sizeof(value));
}

bool MaterialSystem::SetUInt(MaterialId id, const char* name, u32 value)
{
    return SetValue(id, name, MemberKind::Scalar, ScalarType::UInt32, 1, 1, &value, sizeof(value));
}

bool MaterialSystem::SetVec2(MaterialId id, const char* name, const Vec2& value)
{
    const f32 d[2] = {value.x, value.y};
    return SetValue(id, name, MemberKind::Vector, ScalarType::Float32, 2, 1, d, sizeof(d));
}

bool MaterialSystem::SetVec3(MaterialId id, const char* name, const Vec3& value)
{
    const f32 d[3] = {value.x, value.y, value.z};
    return SetValue(id, name, MemberKind::Vector, ScalarType::Float32, 3, 1, d, sizeof(d));
}

bool MaterialSystem::SetVec4(MaterialId id, const char* name, const Vec4& value)
{
    const f32 d[4] = {value.x, value.y, value.z, value.w};
    return SetValue(id, name, MemberKind::Vector, ScalarType::Float32, 4, 1, d, sizeof(d));
}

bool MaterialSystem::SetMat4(MaterialId id, const char* name, const Mat4& value)
{
    static_assert(sizeof(Mat4) == 64, "Mat4 is 16 floats");
    return SetValue(id, name, MemberKind::Matrix, ScalarType::Float32, 4, 4, &value, sizeof(Mat4));
}

bool MaterialSystem::SetTexture(MaterialId id, const char* name, rhi::TextureHandle texture)
{
    MaterialSlot& m = Get(id);
    const Variant& v = templates_[m.template_index].variants[m.variant_index];
    for (u32 i = 0; i < v.texture_count; ++i)
    {
        if (CStrCompare(v.textures[i].name, name) == 0)
        {
            m.texture_owners[i] = nullptr;
            if (!(m.textures[i] == texture))
            {
                m.textures[i] = texture;
                m.groups_dirty = true;
            }
            return true;
        }
    }
    RenderLog(LogVerbosity::Error, "material '%s': there is no texture slot '%s'", m.name, name);
    return false;
}

bool MaterialSystem::SetSampler(MaterialId id, const char* name, rhi::SamplerHandle sampler)
{
    MaterialSlot& m = Get(id);
    const Variant& v = templates_[m.template_index].variants[m.variant_index];
    for (u32 i = 0; i < v.sampler_count; ++i)
    {
        if (CStrCompare(v.samplers[i].name, name) == 0)
        {
            m.sampler_owners[i] = nullptr;
            if (!(m.samplers[i] == sampler))
            {
                m.samplers[i] = sampler;
                m.groups_dirty = true;
            }
            return true;
        }
    }
    RenderLog(LogVerbosity::Error, "material '%s': there is no sampler slot '%s'", m.name, name);
    return false;
}

bool MaterialSystem::SetTexture(MaterialId id, const char* name, const GpuTexture* texture)
{
    MaterialSlot& m = Get(id);
    const Variant& v = templates_[m.template_index].variants[m.variant_index];
    for (u32 i = 0; i < v.texture_count; ++i)
    {
        if (CStrCompare(v.textures[i].name, name) == 0)
        {
            m.texture_owners[i] = texture;
            const rhi::TextureHandle handle = texture != nullptr ? texture->Handle() : rhi::TextureHandle{};
            if (!(m.textures[i] == handle))
            {
                m.textures[i] = handle;
                m.groups_dirty = true;
            }
            return true;
        }
    }
    RenderLog(LogVerbosity::Error, "material '%s': there is no texture slot '%s'", m.name, name);
    return false;
}

bool MaterialSystem::SetSampler(MaterialId id, const char* name, const GpuSampler* sampler)
{
    MaterialSlot& m = Get(id);
    const Variant& v = templates_[m.template_index].variants[m.variant_index];
    for (u32 i = 0; i < v.sampler_count; ++i)
    {
        if (CStrCompare(v.samplers[i].name, name) == 0)
        {
            m.sampler_owners[i] = sampler;
            const rhi::SamplerHandle handle = sampler != nullptr ? sampler->Handle() : rhi::SamplerHandle{};
            if (!(m.samplers[i] == handle))
            {
                m.samplers[i] = handle;
                m.groups_dirty = true;
            }
            return true;
        }
    }
    RenderLog(LogVerbosity::Error, "material '%s': there is no sampler slot '%s'", m.name, name);
    return false;
}

void MaterialSystem::BeginFrame(u64 frame_index)
{
    slot_ = static_cast<u32>(frame_index % kSlots);
    begun_ = true;
}

void MaterialSystem::FlushMaterials()
{
    ENGINE_ASSERT(!released_,
                  "MaterialSystem::FlushMaterials: the system is released (a device reset is in progress or failed)");
    ENGINE_ASSERT(begun_, "MaterialSystem::FlushMaterials: BeginFrame was not called");
    for (u32 i = 0; i < material_high_; ++i)
    {
        MaterialSlot& m = materials_[i];
        if (!m.alive)
        {
            continue;
        }
        // A material bound to an owner follows the owner's current handle (it changes after a device reset).
        for (u32 k = 0; k < kMaxMaterialTextures; ++k)
        {
            if (m.texture_owners[k] != nullptr && !(m.textures[k] == m.texture_owners[k]->Handle()))
            {
                m.textures[k] = m.texture_owners[k]->Handle();
                m.groups_dirty = true;
            }
        }
        for (u32 k = 0; k < kMaxMaterialSamplers; ++k)
        {
            if (m.sampler_owners[k] != nullptr && !(m.samplers[k] == m.sampler_owners[k]->Handle()))
            {
                m.samplers[k] = m.sampler_owners[k]->Handle();
                m.groups_dirty = true;
            }
        }
        if (m.groups_dirty)
        {
            if (BuildGroups(m))
            {
                ++m.rebuilds;
                ++stats_.bind_group_rebuilds;
            }
            else
            {
                RenderLog(LogVerbosity::Error,
                          "material '%s': rebuilding the bind groups failed; the old textures stay in use", m.name);
                // Put the slots back to what the groups still show, so the material and its groups agree and the same
                // SetTexture call is a real change again.
                for (u32 k = 0; k < kMaxMaterialTextures; ++k)
                {
                    m.textures[k] = m.applied_textures[k];
                }
                for (u32 k = 0; k < kMaxMaterialSamplers; ++k)
                {
                    m.samplers[k] = m.applied_samplers[k];
                }
                m.groups_dirty = false;
            }
        }
        if ((m.dirty_slots & (1u << slot_)) != 0)
        {
            MemCopy(m.mapped + m.stride * slot_, m.shadow.Data(), m.shadow_size);
            device_->FlushMappedBuffer(m.params, m.stride * slot_, m.shadow_size);
            m.dirty_slots &= ~(1u << slot_);
            ++stats_.parameter_flushes;
        }
    }
}

rhi::BindGroupHandle MaterialSystem::MaterialBindGroup(MaterialId id) const
{
    const MaterialSlot& m = Get(id);
    ENGINE_ASSERT(begun_ && !m.groups_dirty && (m.dirty_slots & (1u << slot_)) == 0,
                  "MaterialSystem::MaterialBindGroup: the material changed since the last FlushMaterials");
    return m.groups[slot_];
}

rhi::TextureHandle MaterialSystem::EffectiveTexture(MaterialId id, const char* name) const
{
    const MaterialSlot& m = Get(id);
    const Variant& v = templates_[m.template_index].variants[m.variant_index];
    for (u32 i = 0; i < v.texture_count; ++i)
    {
        if (CStrCompare(v.textures[i].name, name) == 0)
        {
            const rhi::TextureHandle bound = m.texture_owners[i] != nullptr ? m.texture_owners[i]->Handle()
                                                                            : m.textures[i];
            return bound.IsValid() ? bound : defaults_[static_cast<u32>(v.textures[i].fallback)];
        }
    }
    return rhi::TextureHandle{};
}

BufferRegion MaterialSystem::ParameterRegion(MaterialId id) const
{
    const MaterialSlot& m = Get(id);
    ENGINE_ASSERT(begun_, "MaterialSystem::ParameterRegion: BeginFrame was not called");
    if (!m.params.IsValid())
    {
        return BufferRegion{};
    }
    return BufferRegion{m.params, m.stride * slot_, m.shadow_size};
}

rhi::BindGroupLayoutHandle MaterialSystem::MaterialLayout(MaterialId id) const
{
    const MaterialSlot& m = Get(id);
    return templates_[m.template_index].variants[m.variant_index].layout;
}

u32 MaterialSystem::BindGroupRebuilds(MaterialId id) const
{
    return Get(id).rebuilds;
}

// ---- pipelines ----

rhi::PipelineHandle MaterialSystem::MakePipeline(TemplateSlot& t, const Variant& v, const PipelineTarget& target,
                                                 const VertexLayoutData& vertex_layout, rhi::PrimitiveTopology topology,
                                                 bool primitive_restart)
{
    const char* tname = t.name;
    rhi::VertexBindingDesc bindings[2];
    bindings[0].stride = vertex_layout.stride;
    u32 binding_count = 1;
    rhi::VertexAttributeDesc attributes[rhi::kMaxVertexAttributes];
    u32 attribute_count = vertex_layout.attribute_count;
    for (u32 i = 0; i < attribute_count; ++i)
    {
        attributes[i] = vertex_layout.attributes[i];
    }
    // On Direct3D 12 the object index arrives as an instance-rate attribute from the stream at slot 1
    // (DeviceCaps::object_index_stream).
    if (!AddObjectIndexAttribute(*device_, bindings, &binding_count, 2, attributes, &attribute_count,
                                 rhi::kMaxVertexAttributes, ObjectIndexSlot()))
    {
        RenderLog(LogVerbosity::Error,
                  "MaterialSystem: template '%s': no room for the object index attribute (%u attributes)", tname,
                  attribute_count);
        return {};
    }
    rhi::GraphicsPipelineDesc pd;
    pd.topology = topology;
    pd.primitive_restart = primitive_restart;
    pd.vertex_shader = t.vertex;
    pd.fragment_shader = v.fragment;
    pd.vertex_bindings = rhi::Span<rhi::VertexBindingDesc>(bindings, binding_count);
    pd.vertex_attributes = rhi::Span<rhi::VertexAttributeDesc>(attributes, attribute_count);
    pd.cull_mode = t.desc.state.cull;
    pd.depth_format = target.depth_format;
    pd.depth_test = t.desc.state.depth_test && target.depth_format != rhi::Format::Undefined;
    pd.depth_write = t.desc.state.depth_write && target.depth_format != rhi::Format::Undefined;
    pd.depth_compare = t.desc.state.depth_compare;
    pd.color_target_count = target.color_count;
    for (u32 i = 0; i < target.color_count; ++i)
    {
        pd.color_targets[i].format = target.color_formats[i];
        if (t.desc.state.alpha_blend)
        {
            rhi::BlendState& b = pd.color_targets[i].blend;
            b.enable = true;
            b.src_color = rhi::BlendFactor::SrcAlpha;
            b.dst_color = rhi::BlendFactor::OneMinusSrcAlpha;
            b.src_alpha = rhi::BlendFactor::One;
            b.dst_alpha = rhi::BlendFactor::OneMinusSrcAlpha;
        }
    }
    pd.bind_group_layouts[0] = frame_layout_;
    pd.bind_group_layouts[1] = v.layout;
    pd.bind_group_layouts[2] = object_layout_;
    pd.bind_group_layout_count = 3;
    pd.debug_name = tname;
    const rhi::PipelineHandle pipeline = device_->CreateGraphicsPipeline(pd);
    if (!pipeline.IsValid())
    {
        RenderLog(LogVerbosity::Error, "MaterialSystem: the device could not create the pipeline for '%s'", tname);
    }
    return pipeline;
}

bool MaterialSystem::RebuildPipeline(PipelineEntry& entry)
{
    if (entry.pipeline.IsValid())
    {
        return true;
    }
    TemplateSlot& t = templates_[entry.template_index];
    if (!t.alive || t.generation != entry.template_generation)
    {
        return true;  // the template is gone; the entry is stale
    }
    for (u32 v = 0; v < t.variant_count; ++v)
    {
        if (t.variants[v].key_bits == entry.key_bits)
        {
            entry.pipeline = MakePipeline(t, t.variants[v], entry.target, entry.layout, entry.topology, entry.restart);
            if (entry.pipeline.IsValid())
            {
                ++stats_.pipelines;
            }
            return entry.pipeline.IsValid();
        }
    }
    return true;
}

PipelineLookup MaterialSystem::GetPipeline(MaterialId id, const PipelineTarget& target,
                                           const VertexLayoutData& vertex_layout, rhi::PrimitiveTopology topology,
                                           bool primitive_restart)
{
    const MaterialSlot& m = Get(id);
    TemplateSlot& t = templates_[m.template_index];
    const Variant& v = t.variants[m.variant_index];

    for (u32 i = 0; i < pipeline_count_; ++i)
    {
        const PipelineEntry& p = pipelines_[i];
        if (p.used && p.template_index == m.template_index && p.template_generation == t.generation &&
            p.key_bits == v.key_bits && SameTarget(p.target, target) && SameLayout(p.layout, vertex_layout) &&
            p.topology == topology && p.restart == primitive_restart)
        {
            if (p.pipeline.IsValid())
            {
                ++stats_.pipeline_hits;
                return PipelineLookup{p.pipeline, i};
            }
            // The entry survived a device reset without its pipeline: build it again in the same slot (a lazy rebuild).
            ++stats_.pipeline_misses;
            const rhi::PipelineHandle rebuilt = MakePipeline(t, v, target, vertex_layout, topology, primitive_restart);
            if (!rebuilt.IsValid())
            {
                return PipelineLookup{};
            }
            pipelines_[i].pipeline = rebuilt;
            ++stats_.pipelines;
            return PipelineLookup{rebuilt, i};
        }
    }
    ++stats_.pipeline_misses;

    const char* tname = t.name;
    if (target.color_count > rhi::kMaxColorAttachments ||
        (target.color_count == 0 && target.depth_format == rhi::Format::Undefined))
    {
        RenderLog(LogVerbosity::Error, "MaterialSystem: pipeline for '%s': the target has no attachments or too many",
                  tname);
        return PipelineLookup{};
    }
    for (u32 i = 0; i < target.color_count; ++i)
    {
        if (target.color_formats[i] == rhi::Format::Undefined)
        {
            RenderLog(LogVerbosity::Error, "MaterialSystem: pipeline for '%s': color target %u has no format", tname,
                      i);
            return PipelineLookup{};
        }
    }
    if (vertex_layout.attribute_count > rhi::kMaxVertexAttributes || vertex_layout.stride == 0)
    {
        RenderLog(LogVerbosity::Error, "MaterialSystem: pipeline for '%s': the vertex layout is empty or too large",
                  tname);
        return PipelineLookup{};
    }
    for (u32 i = 0; i < t.vertex_reflection.vertex_input_count; ++i)
    {
        const ReflectedVertexInput& in = t.vertex_reflection.vertex_inputs[i];
        bool found = false;
        for (u32 a = 0; a < vertex_layout.attribute_count; ++a)
        {
            if (vertex_layout.attributes[a].location == in.location)
            {
                found = true;
                const rhi::VertexFormat have = vertex_layout.attributes[a].format;
                // A normalized, half or float attribute reaches the shader as a float vector and an integer attribute
                // as a uint vector of the same width (rhi::VertexFormatFeedsInput).
                const bool compatible = rhi::VertexFormatFeedsInput(have, in.format);
                if (!compatible)
                {
                    RenderLog(
                        LogVerbosity::Error,
                        "MaterialSystem: pipeline for '%s': vertex input '%s' (location %u) has a different format in the mesh layout",
                        tname, in.name, in.location);
                    return PipelineLookup{};
                }
            }
        }
        if (!found)
        {
            RenderLog(
                LogVerbosity::Error,
                "MaterialSystem: pipeline for '%s': the mesh layout has no attribute for vertex input '%s' (location %u)",
                tname, in.name, in.location);
            return PipelineLookup{};
        }
    }

    u32 slot = pipeline_count_;
    for (u32 i = 0; i < pipeline_count_; ++i)
    {
        if (!pipelines_[i].used)
        {
            slot = i;
            break;
        }
    }
    if (slot == desc_.max_pipelines)
    {
        RenderLog(LogVerbosity::Error, "MaterialSystem: pipeline for '%s' not created: the pipeline cache is full",
                  tname);
        return PipelineLookup{};
    }

    if (primitive_restart && topology != rhi::PrimitiveTopology::TriangleStrip &&
        topology != rhi::PrimitiveTopology::LineStrip)
    {
        RenderLog(LogVerbosity::Error, "MaterialSystem: pipeline for '%s': primitive restart needs a strip topology",
                  tname);
        return PipelineLookup{};
    }
    const rhi::PipelineHandle pipeline = MakePipeline(t, v, target, vertex_layout, topology, primitive_restart);
    if (!pipeline.IsValid())
    {
        return PipelineLookup{};
    }
    PipelineEntry& e = pipelines_[slot];
    e.used = true;
    e.template_index = m.template_index;
    e.template_generation = t.generation;
    e.key_bits = v.key_bits;
    e.target = target;
    e.layout = vertex_layout;
    e.topology = topology;
    e.restart = primitive_restart;
    e.pipeline = pipeline;
    if (slot == pipeline_count_)
    {
        ++pipeline_count_;
    }
    ++stats_.pipelines;
    return PipelineLookup{pipeline, slot};
}

bool MaterialSystem::Warmup(MaterialId id, const PipelineTarget& target, const VertexLayoutData& vertex_layout,
                            rhi::PrimitiveTopology topology, bool primitive_restart)
{
    return GetPipeline(id, target, vertex_layout, topology, primitive_restart).pipeline.IsValid();
}

}  // namespace engine_placeholder::renderer
