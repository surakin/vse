// MetalDevice: samplers, bind group layouts and bind groups, and binding them at the slots a Metal library's minted
// slot table gives (Phase 21 Day 302; decision 9: direct set*Buffer/set*Texture/set*SamplerState calls, no argument
// buffers).
//
// Slang chooses each Metal library's slots itself (one table per stage, Day 299), so a group's resources are not bound
// at a fixed slot: a pipeline maps each stage's table against its layouts (MapStageSlots, at creation), and the binder
// sets each resource at the slot that stage's table gives. Groups hold their resources by RHI handle and are resolved
// when they are bound (the reset rule), never cached as Metal objects past a frame.

#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "sync/mutex.h"

#include "algorithm.h"
#include "build_defs.h"

#include "log/assert.h"
#include "rhi/metal/metal_device.h"
#include "rhi/rhi_log.h"

#import <Foundation/Foundation.h>

namespace vse::rhi::metal
{

namespace
{

enum class SlotClass : u8
{
    Buffer,
    Texture,
    Sampler,
};

SlotClass ClassOf(BindingType type)
{
    switch (type)
    {
    case BindingType::UniformBuffer:
    case BindingType::StorageBuffer:
    case BindingType::ReadOnlyStorageBuffer: return SlotClass::Buffer;
    case BindingType::SampledTexture:
    case BindingType::StorageTexture: return SlotClass::Texture;
    case BindingType::Sampler:
    case BindingType::Count: break;
    }
    return SlotClass::Sampler;
}

u32 SlotLimit(SlotClass c)
{
    return c == SlotClass::Buffer    ? kMetalBufferSlots
           : c == SlotClass::Texture ? kMetalTextureSlots
                                     : kMetalSamplerSlots;
}

const char* ClassName(SlotClass c)
{
    return c == SlotClass::Buffer ? "buffer" : c == SlotClass::Texture ? "texture" : "sampler";
}

bool SameSampler(const SamplerDesc& a, const SamplerDesc& b)
{
    return a.mag_filter == b.mag_filter && a.min_filter == b.min_filter && a.mip_filter == b.mip_filter &&
           a.address_u == b.address_u && a.address_v == b.address_v && a.address_w == b.address_w &&
           a.border_color == b.border_color && a.max_anisotropy == b.max_anisotropy &&
           a.compare_enable == b.compare_enable && a.compare_op == b.compare_op && a.min_lod == b.min_lod &&
           a.max_lod == b.max_lod;
}

MTLSamplerMinMagFilter MinMagOf(FilterMode f)
{
    return f == FilterMode::Nearest ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
}

MTLSamplerAddressMode AddressOf(AddressMode a)
{
    switch (a)
    {
    case AddressMode::Repeat: return MTLSamplerAddressModeRepeat;
    case AddressMode::MirrorRepeat: return MTLSamplerAddressModeMirrorRepeat;
    case AddressMode::ClampToEdge: return MTLSamplerAddressModeClampToEdge;
    case AddressMode::ClampToBorder:
#if ENGINE_PLATFORM_MACOS
        return MTLSamplerAddressModeClampToBorderColor;
#else
        return MTLSamplerAddressModeClampToEdge;  // iOS before 14 has no border color (Phase 28 decides)
#endif
    }
    return MTLSamplerAddressModeRepeat;
}

MTLCompareFunction CompareOf(CompareOp op)
{
    switch (op)
    {
    case CompareOp::Never: return MTLCompareFunctionNever;
    case CompareOp::Less: return MTLCompareFunctionLess;
    case CompareOp::Equal: return MTLCompareFunctionEqual;
    case CompareOp::LessEqual: return MTLCompareFunctionLessEqual;
    case CompareOp::Greater: return MTLCompareFunctionGreater;
    case CompareOp::NotEqual: return MTLCompareFunctionNotEqual;
    case CompareOp::GreaterEqual: return MTLCompareFunctionGreaterEqual;
    case CompareOp::Always: return MTLCompareFunctionAlways;
    }
    return MTLCompareFunctionNever;
}

const char* StageName(ShaderStage stage)
{
    return stage == ShaderStage::Vertex ? "vertex" : stage == ShaderStage::Fragment ? "fragment" : "compute";
}

}  // namespace

// ---- samplers ----

SamplerHandle MetalDevice::CreateSampler(const SamplerDesc& desc)
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
    SamplerRes res;
    res.desc = normalized;
    @autoreleasepool
    {
        MTLSamplerDescriptor* d = [[MTLSamplerDescriptor alloc] init];
        d.minFilter = MinMagOf(normalized.min_filter);
        d.magFilter = MinMagOf(normalized.mag_filter);
        d.mipFilter = normalized.mip_filter == FilterMode::Nearest ? MTLSamplerMipFilterNearest
                                                                   : MTLSamplerMipFilterLinear;
        d.sAddressMode = AddressOf(normalized.address_u);
        d.tAddressMode = AddressOf(normalized.address_v);
        d.rAddressMode = AddressOf(normalized.address_w);
#if ENGINE_PLATFORM_MACOS
        d.borderColor = normalized.border_color == BorderColor::OpaqueWhite   ? MTLSamplerBorderColorOpaqueWhite
                        : normalized.border_color == BorderColor::OpaqueBlack ? MTLSamplerBorderColorOpaqueBlack
                                                                              : MTLSamplerBorderColorTransparentBlack;
#endif
        const f32 aniso = normalized.max_anisotropy < 1.0f ? 1.0f : normalized.max_anisotropy;
        d.maxAnisotropy = static_cast<NSUInteger>(aniso);
        d.compareFunction = normalized.compare_enable ? CompareOf(normalized.compare_op) : MTLCompareFunctionNever;
        d.lodMinClamp = normalized.min_lod;
        d.lodMaxClamp = normalized.max_lod;
        if (desc.debug_name != nullptr)  // Metal's validation refuses a nil label
        {
            d.label = [NSString stringWithUTF8String:desc.debug_name];
        }
        res.state = [device_ newSamplerStateWithDescriptor:d];
    }
    if (res.state == nil)
    {
        RhiLog(LogVerbosity::Error, "CreateSampler: Metal could not create the sampler");
        return {};
    }
    // Equal descriptions share one sampler (Metal's sampler states are immutable): looked up and added under one lock,
    // so two threads asking for the same description at once get one handle (the state made in vain is dropped). A
    // shared handle counts its creators, as on Direct3D 12.
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
    const SamplerHandle handle = samplers_.Allocate(static_cast<SamplerRes&&>(res));
    if (!handle.IsValid())
    {
        RhiLog(LogVerbosity::Error, "CreateSampler: out of sampler handles");
    }
    return handle;
}

void MetalDevice::DestroySampler(SamplerHandle sampler)
{
    LockGuard<Mutex> lock(resource_mutex_);
    ENGINE_ASSERT(samplers_.Get(sampler) != nullptr, "DestroySampler: handle is invalid or already destroyed");
    ReleaseSamplerLocked(sampler);
}

void MetalDevice::ReleaseSamplerLocked(SamplerHandle sampler)
{
    SamplerRes* s = samplers_.Get(sampler);
    if (s == nullptr)
    {
        return;
    }
    if (--s->refs == 0)
    {
        samplers_.Retire(sampler, frame_open_.Load(MemoryOrder::Relaxed) ? frame_number_ : frame_number_ + 1);
    }
}

id<MTLSamplerState> MetalDevice::SamplerObject(SamplerHandle sampler) const
{
    LockGuard<Mutex> lock(resource_mutex_);
    const SamplerRes* s = samplers_.Get(sampler);
    return s != nullptr ? s->state : nil;
}

// ---- layouts ----

BindGroupLayoutHandle MetalDevice::CreateBindGroupLayout(const BindGroupLayoutDesc& desc)
{
    if (const char* why = desc.Validate())
    {
        RhiLog(LogVerbosity::Error, "CreateBindGroupLayout: %s", why);
        return {};
    }
    BindGroupLayoutRes res;
    // Sorted by binding index, so lookups and comparisons do not depend on the order the entries were given in.
    for (usize i = 0; i < desc.entries.Size(); ++i)
    {
        res.bindings.Push(desc.entries[i]);
    }
    Sort(res.bindings, [](const BindingDesc& a, const BindingDesc& b) { return a.binding < b.binding; });
    LockGuard<Mutex> lock(resource_mutex_);
    const BindGroupLayoutHandle handle = bind_group_layouts_.Allocate(static_cast<BindGroupLayoutRes&&>(res));
    if (!handle.IsValid())
    {
        RhiLog(LogVerbosity::Error, "CreateBindGroupLayout: out of layout handles");
    }
    return handle;
}

void MetalDevice::DestroyBindGroupLayout(BindGroupLayoutHandle layout)
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

bool MetalDevice::LayoutBindings(BindGroupLayoutHandle layout, Vector<BindingDesc>* out) const
{
    LockGuard<Mutex> lock(resource_mutex_);
    const BindGroupLayoutRes* l = bind_group_layouts_.Get(layout);
    if (l == nullptr)
    {
        return false;
    }
    out->Clear();
    for (usize i = 0; i < l->bindings.Size(); ++i)
    {
        out->Push(l->bindings[i]);
    }
    return true;
}

// ---- groups ----

BindGroupHandle MetalDevice::CreateBindGroup(const BindGroupDesc& desc)
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
    u32 total = 0;
    for (usize b = 0; b < layout->bindings.Size(); ++b)
    {
        total += layout->bindings[b].count;
    }
    if (desc.entries.Size() != total)
    {
        RhiLog(LogVerbosity::Error, "CreateBindGroup: %u entries given but the layout has %u binding elements",
               static_cast<u32>(desc.entries.Size()), total);
        return {};
    }
    BindGroupRes res;
    res.transient = desc.transient;
    for (usize b = 0; b < layout->bindings.Size(); ++b)
    {
        const BindingDesc& bd = layout->bindings[b];
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
                return {};
            }
            GroupEntry entry;
            entry.binding = bd.binding;
            entry.element = e;
            entry.type = bd.type;
            switch (ClassOf(bd.type))
            {
            case SlotClass::Buffer:
            {
                const bool uniform = bd.type == BindingType::UniformBuffer;
                const BufferRes* buf = buffers_.Get(found->buffer);
                if (buf == nullptr)
                {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u buffer is invalid or destroyed",
                           bd.binding);
                    return {};
                }
                if (!Has(buf->desc.usage, uniform ? BufferUsage::Uniform : BufferUsage::Storage))
                {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u buffer lacks %s usage", bd.binding,
                           uniform ? "Uniform" : "Storage");
                    return {};
                }
                const u32 align = uniform ? caps_.min_uniform_buffer_offset_alignment
                                          : caps_.min_storage_buffer_offset_alignment;
                if (align != 0 && found->offset % align != 0)
                {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u offset is not a multiple of %u",
                           bd.binding, align);
                    return {};
                }
                if (found->offset >= buf->desc.size ||
                    (found->size != 0 && found->size > buf->desc.size - found->offset))
                {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u range lies outside the buffer",
                           bd.binding);
                    return {};
                }
                entry.buffer = found->buffer;
                entry.offset = found->offset;
                break;
            }
            case SlotClass::Texture:
            {
                const TextureRes* tex = textures_.Get(found->texture);
                if (tex == nullptr)
                {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u texture is invalid or destroyed",
                           bd.binding);
                    return {};
                }
                const TextureUsage need = bd.type == BindingType::StorageTexture ? TextureUsage::Storage
                                                                                 : TextureUsage::Sampled;
                if (!Has(tex->desc.usage, need))
                {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u texture lacks %s usage", bd.binding,
                           need == TextureUsage::Storage ? "Storage" : "Sampled");
                    return {};
                }
                entry.texture = found->texture;
                break;
            }
            case SlotClass::Sampler:
            {
                if (samplers_.Get(found->sampler) == nullptr)
                {
                    RhiLog(LogVerbosity::Error, "CreateBindGroup: binding %u sampler is invalid or destroyed",
                           bd.binding);
                    return {};
                }
                entry.sampler = found->sampler;
                break;
            }
            }
            res.entries.Push(entry);
        }
    }
    const BindGroupHandle handle = bind_groups_.Allocate(static_cast<BindGroupRes&&>(res));
    if (!handle.IsValid())
    {
        RhiLog(LogVerbosity::Error, "CreateBindGroup: out of bind group handles");
        return {};
    }
    if (desc.transient)
    {
        frame_groups_[frame_number_ % kFrameSlots].Push(handle);
    }
    return handle;
}

void MetalDevice::DestroyBindGroup(BindGroupHandle group)
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

void MetalDevice::RetireFrameGroupsLocked(u32 slot)
{
    // The frame that last used this slot has finished (BeginFrame waited for frame_number - frames_in_flight, and the
    // slots outnumber the frames in flight): its transient groups end, stamped with the open frame.
    Vector<BindGroupHandle>& groups = frame_groups_[slot];
    for (usize i = 0; i < groups.Size(); ++i)
    {
        bind_groups_.Retire(groups[i], frame_number_);
    }
    groups.Clear();
}

bool MetalDevice::ResolveGroup(BindGroupHandle group, Vector<MetalGroupResource>* out) const
{
    out->Clear();
    LockGuard<Mutex> lock(resource_mutex_);
    const BindGroupRes* g = bind_groups_.Get(group);
    if (g == nullptr)
    {
        return false;
    }
    for (usize i = 0; i < g->entries.Size(); ++i)
    {
        const GroupEntry& e = g->entries[i];
        MetalGroupResource r;
        r.binding = e.binding;
        r.element = e.element;
        r.type = e.type;
        switch (ClassOf(e.type))
        {
        case SlotClass::Buffer:
        {
            const BufferRes* b = buffers_.Get(e.buffer);
            if (b == nullptr || b->buffer == nil)
            {
                return false;  // destroyed since the group was made: the group is invalid
            }
            r.buffer = b->buffer;
            r.offset = e.offset;
            break;
        }
        case SlotClass::Texture:
        {
            const TextureRes* t = textures_.Get(e.texture);
            if (t == nullptr || t->texture == nil)
            {
                return false;
            }
            r.texture = t->texture;
            break;
        }
        case SlotClass::Sampler:
        {
            const SamplerRes* s = samplers_.Get(e.sampler);
            if (s == nullptr || s->state == nil)
            {
                return false;
            }
            r.sampler = s->state;
            break;
        }
        }
        out->Push(r);
    }
    return true;
}

// ---- slots ----

bool MapStageSlots(MetalDevice& device, ShaderStage stage, const ShaderSlotTable* table,
                   const BindGroupLayoutHandle* layouts, u32 layout_count, MetalStageSlots* out, char* error,
                   usize error_size)
{
    out->refs.Clear();
    out->push_constant_slot = table != nullptr ? table->push_constant_slot : kNoShaderSlot;
    // Each set's layout, read once.
    Array<Vector<BindingDesc>, kMaxBindGroups> sets;
    Array<bool, kMaxBindGroups> valid = {};
    for (u32 s = 0; s < layout_count && s < kMaxBindGroups; ++s)
    {
        valid[s] = device.LayoutBindings(layouts[s], &sets[s]);
    }
    if (table == nullptr)
    {
        // A library minted without its slots (or a hand-made blob without them) cannot be bound: refused when the
        // pipeline gives this stage anything to bind.
        for (u32 s = 0; s < layout_count && s < kMaxBindGroups; ++s)
        {
            for (usize b = 0; valid[s] && b < sets[s].Size(); ++b)
            {
                if (Has(sets[s][b].stages, stage))
                {
                    FormatTo(error, error_size,
                             "the %s shader has no slot table (rhi::ShaderDesc::slots), but set %u binding %u is "
                             "visible to it",
                             StageName(stage), s, sets[s][b].binding);
                    return false;
                }
            }
        }
        return true;
    }
    if (out->push_constant_slot != kNoShaderSlot && out->push_constant_slot >= kMetalBufferSlots)
    {
        FormatTo(error, error_size, "the %s shader's push constants are at buffer slot %u, outside Metal's table",
                 StageName(stage), out->push_constant_slot);
        return false;
    }
    for (u32 i = 0; i < table->count; ++i)
    {
        const ShaderSlot& s = table->slots[i];
        if (s.set >= layout_count || s.set >= kMaxBindGroups || !valid[s.set])
        {
            FormatTo(error, error_size,
                     "the %s shader binds set %u binding %u, but the pipeline has no valid layout for set %u",
                     StageName(stage), s.set, s.binding, s.set);
            return false;
        }
        const Vector<BindingDesc>& bindings = sets[s.set];
        const BindingDesc* found = nullptr;
        for (usize b = 0; b < bindings.Size() && found == nullptr; ++b)
        {
            if (bindings[b].binding == s.binding)
            {
                found = &bindings[b];
            }
        }
        if (found == nullptr)
        {
            FormatTo(error, error_size, "the %s shader binds set %u binding %u, which the set's layout lacks",
                     StageName(stage), s.set, s.binding);
            return false;
        }
        if (!Has(found->stages, stage))
        {
            FormatTo(error, error_size, "set %u binding %u is used by the %s shader but not visible to that stage",
                     s.set, s.binding, StageName(stage));
            return false;
        }
        const SlotClass cls = ClassOf(found->type);
        if (ClassOf(s.type) != cls)
        {
            FormatTo(error, error_size,
                     "set %u binding %u is a %s in the %s shader but a %s in the layout (another Metal table)", s.set,
                     s.binding, ClassName(ClassOf(s.type)), StageName(stage), ClassName(cls));
            return false;
        }
        if (s.count != found->count)
        {
            FormatTo(error, error_size, "set %u binding %u has %u elements in the %s shader but %u in the layout",
                     s.set, s.binding, s.count, StageName(stage), found->count);
            return false;
        }
        if (s.slot + found->count > SlotLimit(cls) ||
            (cls == SlotClass::Buffer && out->push_constant_slot != kNoShaderSlot &&
             out->push_constant_slot >= s.slot && out->push_constant_slot < s.slot + found->count))
        {
            FormatTo(error, error_size,
                     "set %u binding %u has the %s shader's slot %u, outside Metal's table or on the push constants",
                     s.set, s.binding, StageName(stage), s.slot);
            return false;
        }
        for (usize r = 0; r < out->refs.Size(); ++r)
        {
            const MetalSlotRef& o = out->refs[r];
            if (ClassOf(o.type) == cls && o.slot < s.slot + found->count && s.slot < o.slot + o.count)
            {
                FormatTo(error, error_size, "set %u binding %u and set %u binding %u share the %s shader's slot %u",
                         o.set, o.binding, s.set, s.binding, StageName(stage), s.slot);
                return false;
            }
        }
        MetalSlotRef ref;
        ref.set = s.set;
        ref.binding = s.binding;
        ref.slot = s.slot;
        ref.type = found->type;
        ref.count = found->count;
        out->refs.Push(ref);
    }
    // Sorted by (set, binding) once here, so the binder finds a group's resource by a binary search on every bind
    // (container audit Day 12); stable, so of two refs for one binding the first stays the one found, as before.
    StableSort(out->refs, [](const MetalSlotRef& a, const MetalSlotRef& b)
               { return SlotRefKey(a.set, a.binding) < SlotRefKey(b.set, b.binding); });
    return true;
}

// ---- the binder ----

void MetalBinder::Reset()
{
    vertex_ = Bound{};
    fragment_ = Bound{};
    compute_ = Bound{};
    issued_ = 0;
}

namespace
{

// The stage's slot ref for one group resource at set `set`, or null when the stage does not bind it. A ref of another
// argument table (the group's layout disagrees with the pipeline's) sets `*mismatch`.
const MetalSlotRef* RefFor(const MetalStageSlots& slots, u32 set, const MetalGroupResource& r, bool* mismatch)
{
    if (set > 0xff || r.binding > 0xff)
    {
        return nullptr;  // a ref's set and binding are bytes: nothing can match
    }
    const u32 key = SlotRefKey(set, r.binding);
    const usize at = LowerBound(slots.refs, key,
                                [](const MetalSlotRef& ref, u32 k) { return SlotRefKey(ref.set, ref.binding) < k; });
    if (at == slots.refs.Size() || SlotRefKey(slots.refs[at].set, slots.refs[at].binding) != key)
    {
        return nullptr;
    }
    const MetalSlotRef& ref = slots.refs[at];
    if (ClassOf(ref.type) != ClassOf(r.type) || r.element >= ref.count)
    {
        *mismatch = true;
        return nullptr;
    }
    return &ref;
}

// Sets one resource on a stage through the stage's three setters, by the binding's class, skipping a slot that already
// holds it; returns the number of calls issued.
template <typename SetBuffer, typename SetTexture, typename SetSampler>
u32 Apply(MetalBinder::Bound& bound, const MetalSlotRef& ref, const MetalGroupResource& r, SetBuffer set_buffer,
          SetTexture set_texture, SetSampler set_sampler)
{
    const u32 slot = ref.slot + r.element;  // inside the table: MapStageSlots checked slot + count
    switch (ClassOf(ref.type))
    {
    case SlotClass::Buffer:
    {
        const void* p = (__bridge const void*)r.buffer;
        if (bound.buffers[slot] == p && bound.offsets[slot] == r.offset)
        {
            return 0;
        }
        bound.buffers[slot] = p;
        bound.offsets[slot] = r.offset;
        set_buffer(r.buffer, r.offset, slot);
        return 1;
    }
    case SlotClass::Texture:
    {
        const void* p = (__bridge const void*)r.texture;
        if (bound.textures[slot] == p)
        {
            return 0;
        }
        bound.textures[slot] = p;
        set_texture(r.texture, slot);
        return 1;
    }
    case SlotClass::Sampler:
    {
        const void* p = (__bridge const void*)r.sampler;
        if (bound.samplers[slot] == p)
        {
            return 0;
        }
        bound.samplers[slot] = p;
        set_sampler(r.sampler, slot);
        return 1;
    }
    }
    return 0;
}

void BindFailed(u32 set, const char* why)
{
    RhiLog(LogVerbosity::Error, "SetBindGroup: the group at set %u %s", set, why);
}

}  // namespace

bool MetalBinder::BindRender(MetalDevice& device, id<MTLRenderCommandEncoder> encoder, const MetalStageSlots* vertex,
                             const MetalStageSlots* fragment, u32 set, BindGroupHandle group)
{
    if (!device.ResolveGroup(group, &scratch_))
    {
        BindFailed(set, "is invalid, or a resource of it was destroyed");
        return false;
    }
    bool mismatch = false;
    for (usize i = 0; i < scratch_.Size(); ++i)
    {
        const MetalGroupResource& r = scratch_[i];
        if (const MetalSlotRef* ref = vertex != nullptr ? RefFor(*vertex, set, r, &mismatch) : nullptr)
        {
            issued_ += Apply(
                vertex_, *ref, r,
                [&](id<MTLBuffer> b, u64 o, u32 s) { [encoder setVertexBuffer:b offset:o atIndex:s]; },
                [&](id<MTLTexture> t, u32 s) { [encoder setVertexTexture:t atIndex:s]; },
                [&](id<MTLSamplerState> st, u32 s) { [encoder setVertexSamplerState:st atIndex:s]; });
        }
        if (const MetalSlotRef* ref = fragment != nullptr ? RefFor(*fragment, set, r, &mismatch) : nullptr)
        {
            issued_ += Apply(
                fragment_, *ref, r,
                [&](id<MTLBuffer> b, u64 o, u32 s) { [encoder setFragmentBuffer:b offset:o atIndex:s]; },
                [&](id<MTLTexture> t, u32 s) { [encoder setFragmentTexture:t atIndex:s]; },
                [&](id<MTLSamplerState> st, u32 s) { [encoder setFragmentSamplerState:st atIndex:s]; });
        }
    }
    scratch_.Clear();  // releases the objects; the command buffer keeps what it uses
    if (mismatch)
    {
        BindFailed(set, "does not match the pipeline's layout for that set (a binding of another type or size)");
        return false;
    }
    return true;
}

bool MetalBinder::BindCompute(MetalDevice& device, id<MTLComputeCommandEncoder> encoder, const MetalStageSlots& compute,
                              u32 set, BindGroupHandle group)
{
    if (!device.ResolveGroup(group, &scratch_))
    {
        BindFailed(set, "is invalid, or a resource of it was destroyed");
        return false;
    }
    bool mismatch = false;
    for (usize i = 0; i < scratch_.Size(); ++i)
    {
        const MetalGroupResource& r = scratch_[i];
        if (const MetalSlotRef* ref = RefFor(compute, set, r, &mismatch))
        {
            issued_ += Apply(
                compute_, *ref, r, [&](id<MTLBuffer> b, u64 o, u32 s) { [encoder setBuffer:b offset:o atIndex:s]; },
                [&](id<MTLTexture> t, u32 s) { [encoder setTexture:t atIndex:s]; },
                [&](id<MTLSamplerState> st, u32 s) { [encoder setSamplerState:st atIndex:s]; });
        }
    }
    scratch_.Clear();
    if (mismatch)
    {
        BindFailed(set, "does not match the pipeline's layout for that set (a binding of another type or size)");
        return false;
    }
    return true;
}

void MetalBinder::PushRender(id<MTLRenderCommandEncoder> encoder, const MetalStageSlots* vertex,
                             const MetalStageSlots* fragment, const void* data, u32 size)
{
    ENGINE_ASSERT(size <= kMaxPushConstantBytes && (size == 0 || data != nullptr),
                  "PushConstants: more than kMaxPushConstantBytes, or no data");
    if (vertex != nullptr && vertex->push_constant_slot != kNoShaderSlot)
    {
        [encoder setVertexBytes:data length:size atIndex:vertex->push_constant_slot];
        vertex_.buffers[vertex->push_constant_slot] = nullptr;  // the slot no longer holds a group's buffer
        ++issued_;
    }
    if (fragment != nullptr && fragment->push_constant_slot != kNoShaderSlot)
    {
        [encoder setFragmentBytes:data length:size atIndex:fragment->push_constant_slot];
        fragment_.buffers[fragment->push_constant_slot] = nullptr;
        ++issued_;
    }
}

void MetalBinder::PushCompute(id<MTLComputeCommandEncoder> encoder, const MetalStageSlots& compute, const void* data,
                              u32 size)
{
    ENGINE_ASSERT(size <= kMaxPushConstantBytes && (size == 0 || data != nullptr),
                  "PushConstants: more than kMaxPushConstantBytes, or no data");
    if (compute.push_constant_slot != kNoShaderSlot)
    {
        [encoder setBytes:data length:size atIndex:compute.push_constant_slot];
        compute_.buffers[compute.push_constant_slot] = nullptr;
        ++issued_;
    }
}

}  // namespace vse::rhi::metal
