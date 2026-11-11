// Dx12Device: samplers, bind group layouts, bind groups and root signatures (Phase 13 Day 166).
//
// A bind group holds its CBV/SRV/UAV descriptors contiguously in a CPU heap (buffer views made for the group's ranges,
// texture views copied from the texture's own), so `SetBindGroup` copies them into the frame's slot of the
// shader-visible ring in one call. Samplers are few and long-lived, and the shader-visible sampler heap holds only
// 2048: a group's samplers are copied once, into a table shared by every group with the same samplers. A root signature
// has one CBV/SRV/UAV table and one sampler table per group and root constants for the push constants; registers follow
// the bindings, spaces the group slots (shaders/target_common.slang).

#include "cstr/mem_ops.h"
#include "sync/atomic.h"
#include "sync/mutex.h"

#include "log/assert.h"
#include "rhi/dx12/dx12_device.h"
#include "rhi/dx12/dx12_pipeline_cache.h"
#include "rhi/dx12/dx12_result.h"
#include "rhi/rhi_log.h"

namespace engine_placeholder::rhi::dx12
{

namespace
{

bool IsStorage(BindingType t)
{
    return t == BindingType::StorageBuffer || t == BindingType::ReadOnlyStorageBuffer;
}

D3D12_FILTER_TYPE FilterType(FilterMode m)
{
    return m == FilterMode::Nearest ? D3D12_FILTER_TYPE_POINT : D3D12_FILTER_TYPE_LINEAR;
}

D3D12_TEXTURE_ADDRESS_MODE AddressOf(AddressMode m)
{
    switch (m)
    {
    case AddressMode::Repeat: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case AddressMode::MirrorRepeat: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case AddressMode::ClampToEdge: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case AddressMode::ClampToBorder: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    }
    return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
}

D3D12_COMPARISON_FUNC CompareOf(CompareOp op)
{
    switch (op)
    {
    case CompareOp::Never: return D3D12_COMPARISON_FUNC_NEVER;
    case CompareOp::Less: return D3D12_COMPARISON_FUNC_LESS;
    case CompareOp::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
    case CompareOp::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case CompareOp::Greater: return D3D12_COMPARISON_FUNC_GREATER;
    case CompareOp::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case CompareOp::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case CompareOp::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
    }
    return D3D12_COMPARISON_FUNC_NEVER;
}

bool SameSampler(const SamplerDesc& a, const SamplerDesc& b)
{
    return a.mag_filter == b.mag_filter && a.min_filter == b.min_filter && a.mip_filter == b.mip_filter &&
           a.address_u == b.address_u && a.address_v == b.address_v && a.address_w == b.address_w &&
           a.border_color == b.border_color && a.max_anisotropy == b.max_anisotropy &&
           a.compare_enable == b.compare_enable && a.compare_op == b.compare_op && a.min_lod == b.min_lod &&
           a.max_lod == b.max_lod;
}

bool SameBindings(const BindingDesc* a, u32 a_count, const BindingDesc* b, u32 b_count)
{
    if (a_count != b_count)
    {
        return false;
    }
    for (u32 i = 0; i < a_count; ++i)
    {
        if (a[i].binding != b[i].binding || a[i].type != b[i].type || a[i].stages != b[i].stages ||
            a[i].count != b[i].count || a[i].stride != b[i].stride)
        {
            return false;
        }
    }
    return true;
}

D3D12_SHADER_VISIBILITY VisibilityOf(ShaderStage stages, bool compute)
{
    if (compute)
    {
        return D3D12_SHADER_VISIBILITY_ALL;
    }
    if (stages == ShaderStage::Vertex)
    {
        return D3D12_SHADER_VISIBILITY_VERTEX;
    }
    if (stages == ShaderStage::Fragment)
    {
        return D3D12_SHADER_VISIBILITY_PIXEL;
    }
    return D3D12_SHADER_VISIBILITY_ALL;
}

constexpr u32 kPushConstantSpace = 15;     // shaders/target_common.slang: push constants are register(b0, space15)
constexpr u32 kMaxSamplersPerGroup = 256;  // the copy list CreateBindGroup builds on the stack
constexpr u64 kMaxConstantBufferBytes = D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT *
                                        16;  // 64 KiB, the largest constant buffer view

}  // namespace

// ---- setup ----

bool Dx12Device::InitializeBindings()
{
    const u32 seed = GenerationSeed(epoch_);
    samplers_.SeedGeneration(seed);
    bind_group_layouts_.SeedGeneration(seed);
    bind_groups_.SeedGeneration(seed);
    sampler_cpu_heap_.Initialize(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    group_views_.Initialize(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return view_ring_.Initialize(device_.Get(), kViewsPerFrame, kFrameSlots) &&
           sampler_tables_heap_.Initialize(device_.Get());
}

void Dx12Device::ShutdownBindings()
{
    bind_groups_.ForEachLive([&](BindGroupHandle, BindGroupRes& g) { ReleaseBindGroupLocked(g); });
    root_signatures_.Clear();
    sampler_tables_.Clear();
    view_ring_.Shutdown();
    sampler_tables_heap_.Shutdown();
    group_views_.Shutdown();
    sampler_cpu_heap_.Shutdown();
}

u32 Dx12Device::FreeSamplerDescriptors() const
{
    LockGuard<Mutex> lock(resource_mutex_);
    return sampler_tables_heap_.FreeCount();
}

usize Dx12Device::RootSignatureCount() const
{
    LockGuard<Mutex> lock(resource_mutex_);
    return root_signatures_.Size();
}

// ---- samplers ----

SamplerHandle Dx12Device::CreateSampler(const SamplerDesc& desc)
{
    if (const char* why = desc.Validate())
    {
        RhiLog(LogVerbosity::Error, "CreateSampler: %s", why);
        return {};
    }
    SamplerDesc normalized = desc;
    normalized.debug_name = nullptr;
    if (normalized.max_anisotropy > caps_.max_sampler_anisotropy)
    {
        normalized.max_anisotropy = caps_.max_sampler_anisotropy;
    }
    LockGuard<Mutex> lock(resource_mutex_);
    SamplerHandle shared;
    samplers_.ForEachLive(
        [&](SamplerHandle h, SamplerRes& s)
        {
            if (!shared.IsValid() && SameSampler(s.desc, normalized))
            {
                ++s.refs;
                shared = h;
            }
        });
    if (shared.IsValid())
    {
        return shared;
    }
    D3D12_SAMPLER_DESC sd{};
    const D3D12_FILTER_REDUCTION_TYPE reduction = normalized.compare_enable ? D3D12_FILTER_REDUCTION_TYPE_COMPARISON
                                                                            : D3D12_FILTER_REDUCTION_TYPE_STANDARD;
    sd.Filter = normalized.max_anisotropy > 1.0f
                    ? D3D12_ENCODE_ANISOTROPIC_FILTER(reduction)
                    : D3D12_ENCODE_BASIC_FILTER(FilterType(normalized.min_filter), FilterType(normalized.mag_filter),
                                                FilterType(normalized.mip_filter), reduction);
    sd.AddressU = AddressOf(normalized.address_u);
    sd.AddressV = AddressOf(normalized.address_v);
    sd.AddressW = AddressOf(normalized.address_w);
    sd.MaxAnisotropy = static_cast<UINT>(normalized.max_anisotropy);
    sd.ComparisonFunc = normalized.compare_enable ? CompareOf(normalized.compare_op) : D3D12_COMPARISON_FUNC_NEVER;
    const f32 alpha = normalized.border_color == BorderColor::TransparentBlack ? 0.0f : 1.0f;
    const f32 rgb = normalized.border_color == BorderColor::OpaqueWhite ? 1.0f : 0.0f;
    sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = rgb;
    sd.BorderColor[3] = alpha;
    sd.MinLOD = normalized.min_lod;
    sd.MaxLOD = normalized.max_lod;
    SamplerRes res;
    res.desc = normalized;
    res.refs = 1;
    if (!sampler_cpu_heap_.Allocate(&res.cpu))
    {
        return {};
    }
    device_->CreateSampler(&sd, res.cpu);
    const SamplerHandle handle = samplers_.Allocate(static_cast<SamplerRes&&>(res));
    if (!handle.IsValid())
    {
        RhiLog(LogVerbosity::Error, "CreateSampler: out of sampler handles");
        sampler_cpu_heap_.Free(res.cpu);
    }
    return handle;
}

void Dx12Device::DestroySampler(SamplerHandle sampler)
{
    LockGuard<Mutex> lock(resource_mutex_);
    SamplerRes* s = samplers_.Get(sampler);
    ENGINE_ASSERT(s != nullptr, "DestroySampler: handle is invalid or already destroyed");
    if (s == nullptr || --s->refs > 0)
    {
        return;
    }
    samplers_.Retire(sampler, frame_open_.Load(MemoryOrder::Relaxed) ? frame_number_ : frame_number_ + 1);
}

// ---- bind group layouts ----

BindGroupLayoutHandle Dx12Device::CreateBindGroupLayout(const BindGroupLayoutDesc& desc)
{
    if (const char* why = desc.Validate())
    {
        RhiLog(LogVerbosity::Error, "CreateBindGroupLayout: %s", why);
        return {};
    }
    BindGroupLayoutRes res;
    res.binding_count = static_cast<u32>(desc.entries.Size());
    BindingDesc sorted[kMaxBindingsPerLayout];
    // Sorted by binding index, so layouts that differ only in the order of their entries are equal and share an
    // identity.
    for (u32 i = 0; i < res.binding_count; ++i)
    {
        u32 at = i;
        while (at > 0 && sorted[at - 1].binding > desc.entries[i].binding)
        {
            sorted[at] = sorted[at - 1];
            --at;
        }
        sorted[at] = desc.entries[i];
    }
    for (u32 i = 0; i < res.binding_count; ++i)
    {
        const BindingDesc& b = sorted[i];
        if (IsStorage(b.type) && b.stride == 0)
        {
            RhiLog(
                LogVerbosity::Error,
                "CreateBindGroupLayout: binding %u is a storage buffer without a stride; Direct3D 12 needs the element size "
                "(BindingDesc::stride) for its structured view",
                b.binding);
            return {};
        }
        res.bindings[i].desc = b;
        if (b.type == BindingType::Sampler)
        {
            res.bindings[i].table_offset = res.sampler_count;
            res.sampler_count += b.count;
        }
        else
        {
            res.bindings[i].table_offset = res.view_count;
            res.view_count += b.count;
        }
    }
    if (res.sampler_count > kMaxSamplersPerGroup)
    {
        RhiLog(LogVerbosity::Error, "CreateBindGroupLayout: %u samplers in one group is more than %u",
               res.sampler_count, kMaxSamplersPerGroup);
        return {};
    }
    LockGuard<Mutex> lock(resource_mutex_);
    for (usize i = 0; i < layout_identities_.Size() && res.id == 0; ++i)
    {
        if (SameBindings(layout_identities_[i].bindings, layout_identities_[i].count, sorted, res.binding_count))
        {
            res.id = layout_identities_[i].id;
        }
    }
    if (res.id == 0)
    {
        LayoutIdentity identity;
        for (u32 i = 0; i < res.binding_count; ++i)
        {
            identity.bindings[i] = sorted[i];
        }
        identity.count = res.binding_count;
        identity.id = next_layout_id_++;
        res.id = identity.id;
        layout_identities_.Push(identity);
    }
    const BindGroupLayoutHandle handle = bind_group_layouts_.Allocate(static_cast<BindGroupLayoutRes&&>(res));
    if (!handle.IsValid())
    {
        RhiLog(LogVerbosity::Error, "CreateBindGroupLayout: out of layout handles");
    }
    return handle;
}

void Dx12Device::DestroyBindGroupLayout(BindGroupLayoutHandle layout)
{
    LockGuard<Mutex> lock(resource_mutex_);
    ENGINE_ASSERT(bind_group_layouts_.Get(layout) != nullptr,
                  "DestroyBindGroupLayout: handle is invalid or already destroyed");
    if (bind_group_layouts_.Get(layout) == nullptr)
    {
        return;
    }
    bind_group_layouts_.Retire(layout, frame_open_.Load(MemoryOrder::Relaxed) ? frame_number_ : frame_number_ + 1);
}

// ---- sampler tables ----

u32 Dx12Device::AcquireSamplerTableLocked(const u64* keys, const usize* sources, u32 count)
{
    u32 free_entry = ~0u;
    for (u32 i = 0; i < sampler_tables_.Size(); ++i)
    {
        SamplerTable& t = sampler_tables_[i];
        if (t.refs == 0)
        {
            if (free_entry == ~0u)
            {
                free_entry = i;
            }
            continue;
        }
        if (t.count == count && MemCompare(t.keys.Data(), keys, count * sizeof(u64)) == 0)
        {
            ++t.refs;
            return i;
        }
    }
    u32 offset = 0;
    if (!sampler_tables_heap_.Allocate(count, &offset))
    {
        RhiLog(LogVerbosity::Error,
               "CreateBindGroup: the shader-visible sampler heap (%u descriptors) has no room for %u more",
               GpuDescriptorTables::kCapacity, count);
        return ~0u;
    }
    for (u32 i = 0; i < count; ++i)
    {
        device_->CopyDescriptorsSimple(1, sampler_tables_heap_.Cpu(offset + i), D3D12_CPU_DESCRIPTOR_HANDLE{sources[i]},
                                       D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    }
    if (free_entry == ~0u)
    {
        sampler_tables_.Push(SamplerTable{});
        free_entry = static_cast<u32>(sampler_tables_.Size() - 1);
    }
    SamplerTable& t = sampler_tables_[free_entry];
    t.offset = offset;
    t.count = count;
    t.keys.Clear();
    for (u32 i = 0; i < count; ++i)
    {
        t.keys.Push(keys[i]);
    }
    t.refs = 1;
    return free_entry;
}

void Dx12Device::ReleaseSamplerTableLocked(u32 index)
{
    if (index >= sampler_tables_.Size())
    {
        return;
    }
    SamplerTable& t = sampler_tables_[index];
    if (t.refs != 0 && --t.refs == 0)
    {
        sampler_tables_heap_.Free(t.offset,
                                  t.count);  // the group is collected: the frames that used the table have finished
        t.keys.Clear();
    }
}

// ---- bind groups ----

BindGroupHandle Dx12Device::CreateBindGroup(const BindGroupDesc& desc)
{
    if (const char* why = desc.Validate())
    {
        RhiLog(LogVerbosity::Error, "CreateBindGroup: %s", why);
        return {};
    }
    LockGuard<Mutex> lock(resource_mutex_);
    if (desc.transient)
    {
        ENGINE_ASSERT(frame_open_.Load(MemoryOrder::Relaxed),
                      "CreateBindGroup: a transient group can only be created inside a frame");
    }
    const BindGroupLayoutRes* layout = bind_group_layouts_.Get(desc.layout);
    if (layout == nullptr)
    {
        RhiLog(LogVerbosity::Error, "CreateBindGroup: layout handle is invalid or destroyed");
        return {};
    }
    const u32 total = layout->view_count + layout->sampler_count;
    if (desc.entries.Size() != total)
    {
        RhiLog(LogVerbosity::Error, "CreateBindGroup: %u entries given but the layout has %u binding elements",
               static_cast<u32>(desc.entries.Size()), total);
        return {};
    }
    BindGroupRes res;
    res.layout_id = layout->id;
    res.transient = desc.transient;
    res.frame = frame_number_;
    if (layout->view_count != 0 && !group_views_.Allocate(layout->view_count, &res.views))
    {
        RhiLog(LogVerbosity::Error, "CreateBindGroup: out of descriptor memory");
        return {};
    }
    res.view_count = layout->view_count;
    usize sampler_sources[kMaxSamplersPerGroup] = {};  // the layout has at most this many (CreateBindGroupLayout)
    u64 sampler_keys[kMaxSamplersPerGroup] = {};
    auto fail = [&]()
    {
        group_views_.Free(res.views, res.view_count);
        return BindGroupHandle{};
    };
    const u32 inc = group_views_.Increment();
    for (u32 b = 0; b < layout->binding_count; ++b)
    {
        const BindingDesc& bd = layout->bindings[b].desc;
        for (u32 e = 0; e < bd.count; ++e)
        {
            const BindGroupEntry* found = nullptr;
            for (usize k = 0; k < desc.entries.Size() && found == nullptr; ++k)
            {
                if (desc.entries[k].binding == bd.binding && desc.entries[k].array_element == e)
                {
                    found = &desc.entries[k];
                }
            }
            if (found == nullptr)
            {
                RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u element %u has no entry", bd.binding, e);
                return fail();
            }
            GroupRef ref;
            ref.type = bd.type;
            const u32 slot = layout->bindings[b].table_offset + e;
            const D3D12_CPU_DESCRIPTOR_HANDLE dst{res.views.ptr + static_cast<usize>(slot) * inc};
            switch (bd.type)
            {
            case BindingType::UniformBuffer:
            case BindingType::StorageBuffer:
            case BindingType::ReadOnlyStorageBuffer:
            {
                const bool uniform = bd.type == BindingType::UniformBuffer;
                const BufferRes* buf = buffers_.Get(found->buffer);
                if (buf == nullptr)
                {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u buffer is invalid or destroyed",
                           bd.binding);
                    return fail();
                }
                if (!Has(buf->desc.usage, uniform ? BufferUsage::Uniform : BufferUsage::Storage))
                {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u buffer lacks %s usage", bd.binding,
                           uniform ? "Uniform" : "Storage");
                    return fail();
                }
                const u32 align = uniform ? caps_.min_uniform_buffer_offset_alignment
                                          : caps_.min_storage_buffer_offset_alignment;
                if (align != 0 && found->offset % align != 0)
                {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u offset is not a multiple of %u",
                           bd.binding, align);
                    return fail();
                }
                if (found->offset >= buf->desc.size ||
                    (found->size != 0 && found->size > buf->desc.size - found->offset))
                {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u range lies outside the buffer",
                           bd.binding);
                    return fail();
                }
                const u64 range = found->size != 0 ? found->size : buf->desc.size - found->offset;
                if (uniform && range > kMaxConstantBufferBytes)
                {
                    RhiLog(
                        LogVerbosity::Error,
                        "CreateBindGroup: binding %u range of %llu bytes exceeds the uniform buffer range limit %llu",
                        bd.binding, static_cast<unsigned long long>(range),
                        static_cast<unsigned long long>(kMaxConstantBufferBytes));
                    return fail();
                }
                if (!uniform && range < bd.stride)
                {
                    RhiLog(LogVerbosity::Error,
                           "CreateBindGroup: binding %u range of %llu bytes holds no whole element of %u bytes",
                           bd.binding, static_cast<unsigned long long>(range), bd.stride);
                    return fail();
                }
                const u64 address = buf->resource->GetGPUVirtualAddress() + found->offset;
                if (uniform)
                {
                    D3D12_CONSTANT_BUFFER_VIEW_DESC v{};
                    v.BufferLocation = address;
                    v.SizeInBytes = static_cast<UINT>((range + 255) / 256 *
                                                      256);  // the buffer was rounded up to 256 at creation
                    device_->CreateConstantBufferView(&v, dst);
                }
                else
                {
                    if (found->offset % bd.stride != 0)
                    {
                        RhiLog(LogVerbosity::Error,
                               "CreateBindGroup: binding %u offset %llu is not a multiple of its stride %u", bd.binding,
                               static_cast<unsigned long long>(found->offset), bd.stride);
                        return fail();
                    }
                    if (bd.type == BindingType::StorageBuffer && buf->desc.location != MemoryLocation::GpuOnly)
                    {
                        RhiLog(
                            LogVerbosity::Error,
                            "CreateBindGroup: binding %u is a writable storage buffer in CPU-visible memory, which Direct3D 12 "
                            "cannot write (use GpuOnly, or a ReadOnlyStorageBuffer binding)",
                            bd.binding);
                        return fail();
                    }
                    if (bd.type == BindingType::ReadOnlyStorageBuffer)
                    {
                        D3D12_SHADER_RESOURCE_VIEW_DESC v{};
                        v.Format = DXGI_FORMAT_UNKNOWN;
                        v.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        v.Buffer.FirstElement = found->offset / bd.stride;
                        v.Buffer.NumElements = static_cast<UINT>(range / bd.stride);
                        v.Buffer.StructureByteStride = bd.stride;
                        device_->CreateShaderResourceView(buf->resource.Get(), &v, dst);
                    }
                    else
                    {
                        D3D12_UNORDERED_ACCESS_VIEW_DESC v{};
                        v.Format = DXGI_FORMAT_UNKNOWN;
                        v.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                        v.Buffer.FirstElement = found->offset / bd.stride;
                        v.Buffer.NumElements = static_cast<UINT>(range / bd.stride);
                        v.Buffer.StructureByteStride = bd.stride;
                        device_->CreateUnorderedAccessView(buf->resource.Get(), nullptr, &v, dst);
                    }
                }
                ref.buffer = found->buffer;
                if (buf->desc.location == MemoryLocation::GpuOnly)
                {
                    // The state the command list's tracking needs for it (dx12_command_list.h); upload-heap buffers
                    // have one fixed state.
                    const D3D12_RESOURCE_STATES state = uniform ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
                                                        : bd.type == BindingType::ReadOnlyStorageBuffer
                                                            ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                                                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
                                                            : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                    res.buffer_uses.Push(GroupBufferUse{buf->resource.Get(), state});
                }
                break;
            }
            case BindingType::SampledTexture:
            case BindingType::StorageTexture:
            {
                const bool sampled = bd.type == BindingType::SampledTexture;
                const TextureRes* tex = textures_.Get(found->texture);
                if (tex == nullptr)
                {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u texture is invalid or destroyed",
                           bd.binding);
                    return fail();
                }
                const D3D12_CPU_DESCRIPTOR_HANDLE src = sampled ? tex->srv : tex->uav;
                if (src.ptr == 0)
                {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u texture lacks %s usage", bd.binding,
                           sampled ? "Sampled" : "Storage");
                    return fail();
                }
                device_->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                ref.texture = found->texture;
                break;
            }
            case BindingType::Sampler:
            {
                const SamplerRes* smp = samplers_.Get(found->sampler);
                if (smp == nullptr)
                {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u sampler is invalid or destroyed",
                           bd.binding);
                    return fail();
                }
                sampler_sources[layout->bindings[b].table_offset + e] = smp->cpu.ptr;
                sampler_keys[layout->bindings[b].table_offset + e] = (static_cast<u64>(found->sampler.index) << 32) |
                                                                     found->sampler.generation;
                ref.sampler = found->sampler;
                break;
            }
            case BindingType::Count: break;
            }
            res.refs.Push(ref);
        }
    }
    if (layout->sampler_count != 0)
    {
        res.sampler_table = AcquireSamplerTableLocked(sampler_keys, sampler_sources, layout->sampler_count);
        if (res.sampler_table == ~0u)
        {
            return fail();
        }
    }
    const BindGroupHandle handle = bind_groups_.Allocate(static_cast<BindGroupRes&&>(res));
    if (!handle.IsValid())
    {
        RhiLog(LogVerbosity::Error, "CreateBindGroup: out of bind group handles");
        ReleaseSamplerTableLocked(res.sampler_table);
        return fail();
    }
    if (desc.transient)
    {
        frame_groups_[frame_number_ % kFrameSlots].Push(handle);
    }
    return handle;
}

void Dx12Device::DestroyBindGroup(BindGroupHandle group)
{
    LockGuard<Mutex> lock(resource_mutex_);
    const BindGroupRes* g = bind_groups_.Get(group);
    ENGINE_ASSERT(g != nullptr, "DestroyBindGroup: handle is invalid or already destroyed");
    if (g == nullptr)
    {
        return;
    }
    ENGINE_ASSERT(!g->transient,
                  "DestroyBindGroup: a transient group is released by the device, not destroyed by the application");
    if (g->transient)
    {
        return;
    }
    bind_groups_.Retire(group, frame_open_.Load(MemoryOrder::Relaxed) ? frame_number_ : frame_number_ + 1);
}

void Dx12Device::ReleaseBindGroupLocked(BindGroupRes& group)
{
    group_views_.Free(group.views, group.view_count);
    group.views = D3D12_CPU_DESCRIPTOR_HANDLE{};
    group.view_count = 0;
    ReleaseSamplerTableLocked(group.sampler_table);
    group.sampler_table = ~0u;
    group.refs.Clear();
    group.buffer_uses.Clear();
}

void Dx12Device::RetireFrameGroupsLocked(u32 slot)
{
    // The frame that last used this slot has finished: its transient groups end, stamped with the open frame (no
    // earlier destroy exceeds it, so the pool's retirement order holds).
    Vector<BindGroupHandle>& groups = frame_groups_[slot];
    for (usize i = 0; i < groups.Size(); ++i)
    {
        bind_groups_.Retire(groups[i], frame_number_);
    }
    groups.Clear();
    view_ring_.Reset(slot);
}

bool Dx12Device::LookupBindGroup(BindGroupHandle group, BindGroupInfo* out) const
{
    LockGuard<Mutex> lock(resource_mutex_);
    const BindGroupRes* g = bind_groups_.Get(group);
    if (g == nullptr)
    {
        return false;
    }
    out->views = g->views;
    out->view_count = g->view_count;
    out->sampler_count = 0;
    out->samplers = D3D12_GPU_DESCRIPTOR_HANDLE{};
    if (g->sampler_table < sampler_tables_.Size())
    {
        out->samplers = sampler_tables_heap_.Gpu(sampler_tables_[g->sampler_table].offset);
        out->sampler_count = sampler_tables_[g->sampler_table].count;
    }
    out->layout_id = g->layout_id;
    out->transient = g->transient;
    out->frame = g->frame;
    out->buffer_uses = g->buffer_uses.Data();
    out->buffer_use_count = static_cast<u32>(g->buffer_uses.Size());
    out->resources_live = true;
    for (usize i = 0; i < g->refs.Size() && out->resources_live; ++i)
    {
        const GroupRef& ref = g->refs[i];
        const bool live = ref.type == BindingType::Sampler ? samplers_.Get(ref.sampler) != nullptr
                          : ref.type == BindingType::SampledTexture || ref.type == BindingType::StorageTexture
                              ? textures_.Get(ref.texture) != nullptr
                              : buffers_.Get(ref.buffer) != nullptr;
        out->resources_live = live;
    }
    return true;
}

// ---- root signatures ----

bool Dx12Device::RootSignatureFor(const BindGroupLayoutHandle* layouts, u32 count, u32 push_bytes,
                                  ShaderStage push_stages, bool compute, RootLayout* out)
{
    ENGINE_ASSERT(count <= kMaxBindGroups && push_bytes <= kMaxPushConstantBytes,
                  "RootSignatureFor: too many groups or push bytes");
    LockGuard<Mutex> lock(resource_mutex_);
    const BindGroupLayoutRes* res[kMaxBindGroups] = {};
    u64 ids[kMaxBindGroups] = {};
    for (u32 g = 0; g < count; ++g)
    {
        res[g] = bind_group_layouts_.Get(layouts[g]);
        if (res[g] == nullptr)
        {
            RhiLog(LogVerbosity::Error, "D3D12: a pipeline's bind group layout %u is invalid or destroyed", g);
            return false;
        }
        ids[g] = res[g]->id;
    }
    for (usize i = 0; i < root_signatures_.Size(); ++i)
    {
        const RootSignatureEntry& e = root_signatures_[i];
        if (e.compute == compute && e.push_bytes == push_bytes && e.push_stages == push_stages &&
            e.layout.layout_count == count && MemCompare(e.layout.layout_ids, ids, count * sizeof(u64)) == 0)
        {
            *out = e.layout;
            return true;
        }
    }
    // Every range of every table, then the parameters that point at them.
    D3D12_DESCRIPTOR_RANGE1 ranges[kMaxBindGroups * kMaxBindingsPerLayout];
    u32 range_count = 0;
    D3D12_ROOT_PARAMETER1 params[kMaxBindGroups * 2 + 1];
    u32 param_count = 0;
    RootLayout layout;
    layout.layout_count = count;
    for (u32 g = 0; g < count; ++g)
    {
        layout.layout_ids[g] = ids[g];
        for (int sampler_pass = 0; sampler_pass < 2; ++sampler_pass)
        {
            const bool samplers = sampler_pass == 1;
            const u32 first = range_count;
            ShaderStage stages = ShaderStage::None;
            for (u32 b = 0; b < res[g]->binding_count; ++b)
            {
                const LayoutBinding& lb = res[g]->bindings[b];
                if ((lb.desc.type == BindingType::Sampler) != samplers)
                {
                    continue;
                }
                D3D12_DESCRIPTOR_RANGE1& r = ranges[range_count++];
                r = D3D12_DESCRIPTOR_RANGE1{};
                switch (lb.desc.type)
                {
                case BindingType::UniformBuffer:
                    r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                    r.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
                    break;
                case BindingType::ReadOnlyStorageBuffer:
                case BindingType::SampledTexture:
                    r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                    r.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
                    break;
                case BindingType::StorageBuffer:
                case BindingType::StorageTexture:
                    r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                    r.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
                    break;
                case BindingType::Sampler: r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; break;
                case BindingType::Count: break;
                }
                r.NumDescriptors = lb.desc.count;
                r.BaseShaderRegister = lb.desc.binding;
                r.RegisterSpace = g;
                r.OffsetInDescriptorsFromTableStart = lb.table_offset;
                stages = stages | lb.desc.stages;
            }
            if (range_count == first)
            {
                continue;
            }
            D3D12_ROOT_PARAMETER1& p = params[param_count];
            p = D3D12_ROOT_PARAMETER1{};
            p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable.NumDescriptorRanges = range_count - first;
            p.DescriptorTable.pDescriptorRanges = &ranges[first];
            p.ShaderVisibility = VisibilityOf(stages, compute);
            (samplers ? layout.sampler_param : layout.view_param)[g] = static_cast<i32>(param_count);
            ++param_count;
        }
    }
    if (push_bytes != 0)
    {
        D3D12_ROOT_PARAMETER1& p = params[param_count];
        p = D3D12_ROOT_PARAMETER1{};
        p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        p.Constants.ShaderRegister = 0;
        p.Constants.RegisterSpace = kPushConstantSpace;
        p.Constants.Num32BitValues = (push_bytes + 3) / 4;
        p.ShaderVisibility = VisibilityOf(push_stages, compute);
        layout.push_param = static_cast<i32>(param_count);
        layout.push_values = p.Constants.Num32BitValues;
        ++param_count;
    }
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC vd{};
    vd.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    vd.Desc_1_1.NumParameters = param_count;
    vd.Desc_1_1.pParameters = param_count != 0 ? params : nullptr;
    vd.Desc_1_1.Flags = compute ? D3D12_ROOT_SIGNATURE_FLAG_NONE
                                : D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComRef<ID3DBlob> blob;
    ComRef<ID3DBlob> error;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&vd, blob.Put(), error.Put());
    if (FAILED(hr))
    {
        RhiLog(LogVerbosity::Error, "D3D12: the root signature does not serialize (%s): %s", HresultName(hr),
               error ? static_cast<const char*>(error->GetBufferPointer()) : "no message");
        return false;
    }
    RootSignatureEntry entry;
    hr = device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature),
                                      entry.signature.PutVoid());
    if (!Check(hr, "CreateRootSignature"))
    {
        return false;
    }
    layout.signature = entry.signature.Get();
    layout.content_hash = Fnv1a(blob->GetBufferPointer(), blob->GetBufferSize());
    entry.layout = layout;
    entry.push_bytes = push_bytes;
    entry.push_stages = push_stages;
    entry.compute = compute;
    root_signatures_.Push(static_cast<RootSignatureEntry&&>(entry));
    *out = layout;
    return true;
}

}  // namespace engine_placeholder::rhi::dx12
