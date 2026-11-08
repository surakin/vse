// The Direct3D 12 command list (Phase 13 Day 168): recording every RHI command into an ID3D12GraphicsCommandList, with the resource
// state tracking described in dx12_command_list.h.

#include "rhi/dx12/dx12_command_list.h"

#include <cstring>

#include "log/assert.h"
#include "rhi/dx12/dx12_format.h"
#include "rhi/dx12/dx12_result.h"
#include "rhi/rhi_log.h"

namespace engine_placeholder::rhi::dx12 {

namespace {

constexpr D3D12_RESOURCE_STATES kShaderResource = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr D3D12_RESOURCE_STATES kReadStates = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
                                              kShaderResource | D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

bool IsRead(D3D12_RESOURCE_STATES s) { return s != D3D12_RESOURCE_STATE_COMMON && (s & ~kReadStates) == 0; }

D3D12_RESOURCE_BARRIER ListTransition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
                                  UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.Subresource = subresource;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    return b;
}

D3D12_RESOURCE_BARRIER UavBarrier(ID3D12Resource* resource) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = resource;
    return b;
}

bool IsBufferState(ResourceState s) {
    switch (s) {
    case ResourceState::Undefined:
    case ResourceState::VertexBuffer:
    case ResourceState::IndexBuffer:
    case ResourceState::UniformBuffer:
    case ResourceState::ShaderRead:
    case ResourceState::ShaderWrite:
    case ResourceState::CopySrc:
    case ResourceState::CopyDst: return true;
    default: return false;
    }
}

// A texture's D3D12 state for an RHI state. A depth texture without shader-resource access (D3D12 denies it) reads depth only.
D3D12_RESOURCE_STATES TextureStateFor(ResourceState s, const TextureDesc& desc) {
    D3D12_RESOURCE_STATES state = ToD3D12State(s);
    if (FormatInfo(desc.format).is_depth && !Has(desc.usage, TextureUsage::Sampled)) {
        state &= ~kShaderResource;
        if (state == D3D12_RESOURCE_STATE_COMMON && s == ResourceState::ShaderRead) state = D3D12_RESOURCE_STATE_DEPTH_READ;
    }
    return state;
}

u32 LevelExtent(u32 base, u32 mip) {
    const u32 v = base >> mip;
    return v > 0 ? v : 1;
}

UINT ListSubresource(u32 mip, u32 layer, u32 mips) { return mip + layer * mips; }

// The part of an attachment a pass renders to: its render-target and depth views are of mip 0, layer 0 (Dx12Device::CreateTexture).
// Discarding more would lose the contents of the other levels and layers.
const D3D12_DISCARD_REGION kAttachmentRegion{0, nullptr, 0, 1};

} // namespace

Dx12CommandList::Dx12CommandList(Dx12Device* device, ComRef<ID3D12CommandAllocator> allocator, ComRef<ID3D12GraphicsCommandList> list)
    : device_(device), allocator_(static_cast<ComRef<ID3D12CommandAllocator>&&>(allocator)), list_(static_cast<ComRef<ID3D12GraphicsCommandList>&&>(list)) {}

bool Dx12CommandList::Recycle(u64 frame) {
    if (phase_ == Phase::Recording) {
        list_->Close(); // begun and dropped without End: an allocator cannot be reset under a list still recording into it
    }
    phase_ = Phase::Initial;
    frame_ = frame;
    failed_ = false;
    label_depth_ = 0;
    buffers_.Clear();
    textures_.Clear();
    in_rendering_ = false;
    pass_color_count_ = 0;
    pass_depth_format_ = Format::Undefined;
    pass_resource_count_ = 0;
    pipeline_bound_ = false;
    pipeline_applied_ = false;
    pipeline_ = Dx12Device::PipelineInfo{};
    applied_state_ = nullptr;
    push_written_ = 0;
    for (u32 i = 0; i < kMaxBindGroups; ++i) group_set_[i] = false;
    groups_dirty_ = 0;
    vertex_slot_count_ = 0;
    vertex_dirty_ = false;
    index_bound_ = false;
    // The allocator's last list ran in this slot's previous frame, which has finished (BeginFrame waited for it).
    return device_->Check(allocator_->Reset(), "ID3D12CommandAllocator::Reset");
}

bool Dx12CommandList::Recording(const char* what) const {
    ENGINE_ASSERT(phase_ == Phase::Recording, what);
    return !failed_;
}

void Dx12CommandList::Fail(const char* what, HRESULT hr) {
    if (!failed_) RhiLog(LogVerbosity::Error, "D3D12: %s failed while recording (%s); the list will not be submitted", what, HresultName(hr));
    failed_ = true;
}

Dx12CommandList::BufferState* Dx12CommandList::FindBuffer(ID3D12Resource* resource) {
    for (usize i = buffers_.Size(); i > 0; --i) { // the newest first: a list usually works on the buffer it touched last
        if (buffers_[i - 1].resource == resource) return &buffers_[i - 1];
    }
    BufferState s;
    s.resource = resource;
    buffers_.Push(s);
    return &buffers_[buffers_.Size() - 1];
}

// A use without a barrier: a buffer still in COMMON is promoted implicitly (and more read states may be added that way); otherwise the
// application's barriers put it in the state the use needs.
void Dx12CommandList::UseBuffer(ID3D12Resource* resource, D3D12_RESOURCE_STATES state) {
    BufferState* s = FindBuffer(resource);
    if (s->state == D3D12_RESOURCE_STATE_COMMON) {
        s->state = state;
        s->promoted = true;
    } else if (s->promoted && IsRead(s->state) && IsRead(state)) {
        s->state |= state;
    }
}

Dx12CommandList::TextureUse* Dx12CommandList::FindTexture(ID3D12Resource* resource) {
    for (usize i = textures_.Size(); i > 0; --i) {
        if (textures_[i - 1].resource == resource) return &textures_[i - 1];
    }
    return nullptr;
}

void Dx12CommandList::Begin() {
    ENGINE_ASSERT(phase_ == Phase::Initial, "CommandList::Begin: the list is already recording or was submitted");
    phase_ = Phase::Recording;
    const HRESULT hr = list_->Reset(allocator_.Get(), nullptr);
    if (FAILED(hr)) {
        device_->Check(hr, "ID3D12GraphicsCommandList::Reset");
        Fail("ID3D12GraphicsCommandList::Reset", hr);
        return;
    }
    ID3D12DescriptorHeap* heaps[] = {device_->ViewRing().Heap(), device_->SamplerHeap()};
    list_->SetDescriptorHeaps(2, heaps);
}

void Dx12CommandList::End() {
    const bool usable = Recording("CommandList::End: the list is not recording");
    ENGINE_ASSERT(!in_rendering_, "CommandList::End: a rendering pass is still open");
    ENGINE_ASSERT(label_depth_ == 0, "CommandList::End: debug labels are unbalanced");
    if (usable) {
        const HRESULT hr = list_->Close();
        if (FAILED(hr)) {
            device_->Check(hr, "ID3D12GraphicsCommandList::Close");
            Fail("ID3D12GraphicsCommandList::Close", hr);
        }
    } else {
        list_->Close(); // a failed list is closed too, so its allocator can be reset
    }
    phase_ = Phase::Ended;
}

// ---- copies ----

void Dx12CommandList::CopyBuffer(BufferHandle src, BufferHandle dst, u64 src_offset, u64 dst_offset, u64 size) {
    const bool usable = Recording("CommandList::CopyBuffer: the list is not recording");
    ENGINE_ASSERT(!in_rendering_, "CommandList::CopyBuffer: not allowed inside a rendering pass");
    Dx12Device::BufferInfo s, d;
    ENGINE_ASSERT(device_->LookupBuffer(src, &s), "CommandList::CopyBuffer: stale or invalid source handle");
    ENGINE_ASSERT(device_->LookupBuffer(dst, &d), "CommandList::CopyBuffer: stale or invalid destination handle");
    ENGINE_ASSERT(Has(s.desc.usage, BufferUsage::CopySrc), "CommandList::CopyBuffer: the source lacks CopySrc usage");
    ENGINE_ASSERT(Has(d.desc.usage, BufferUsage::CopyDst), "CommandList::CopyBuffer: the destination lacks CopyDst usage");
    ENGINE_ASSERT(size <= s.desc.size && src_offset <= s.desc.size - size, "CommandList::CopyBuffer: source range outside the buffer");
    ENGINE_ASSERT(size <= d.desc.size && dst_offset <= d.desc.size - size, "CommandList::CopyBuffer: destination range outside the buffer");
    ENGINE_ASSERT(s.resource != d.resource || size == 0 || src_offset + size <= dst_offset || dst_offset + size <= src_offset,
                  "CommandList::CopyBuffer: the ranges overlap within one buffer");
    if (size == 0 || !usable) {
        return;
    }
    if (s.desc.location == MemoryLocation::GpuOnly) UseBuffer(s.resource, D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (d.desc.location == MemoryLocation::GpuOnly) UseBuffer(d.resource, D3D12_RESOURCE_STATE_COPY_DEST);
    list_->CopyBufferRegion(d.resource, dst_offset, s.resource, src_offset, size);
}

void Dx12CommandList::CopyLevel(BufferHandle buffer, TextureHandle texture, const BufferTextureCopy& region, bool to_texture) {
    const char* what = to_texture ? "CommandList::CopyBufferToTexture" : "CommandList::CopyTextureToBuffer";
    const bool usable = Recording(what);
    ENGINE_ASSERT(!in_rendering_, "CommandList copy: not allowed inside a rendering pass");
    Dx12Device::BufferInfo b;
    Dx12Device::TextureInfo t;
    ENGINE_ASSERT(device_->LookupBuffer(buffer, &b), "CommandList copy: stale or invalid buffer handle");
    ENGINE_ASSERT(device_->LookupTexture(texture, &t), "CommandList copy: stale or invalid texture handle");
    ENGINE_ASSERT(Has(b.desc.usage, to_texture ? BufferUsage::CopySrc : BufferUsage::CopyDst), "CommandList copy: the buffer lacks its copy usage");
    ENGINE_ASSERT(Has(t.desc.usage, to_texture ? TextureUsage::CopyDst : TextureUsage::CopySrc), "CommandList copy: the texture lacks its copy usage");
    ENGINE_ASSERT(region.mip_level < t.desc.mip_levels && region.array_layer < t.desc.array_layers, "CommandList copy: mip or layer out of range");
    const FormatProperties& fp = FormatInfo(t.desc.format);
    ENGINE_ASSERT(!fp.has_stencil, "CommandList copy: depth-stencil formats with a stencil aspect cannot be copied");
    const u32 width = LevelExtent(t.desc.width, region.mip_level);
    const u32 height = LevelExtent(t.desc.height, region.mip_level);
    const u32 depth = t.desc.dimension == TextureDimension::Tex3D ? LevelExtent(t.desc.depth, region.mip_level) : 1;
    const u64 row_bytes = FormatRowBytes(t.desc.format, width);
    const u64 pitch = region.buffer_row_pitch != 0 ? region.buffer_row_pitch : row_bytes;
    const DeviceCaps& caps = device_->Caps();
    ENGINE_ASSERT(pitch >= row_bytes && pitch % fp.bytes_per_block == 0 && region.buffer_offset % fp.bytes_per_block == 0,
                  "CommandList copy: buffer_offset or buffer_row_pitch is not a multiple of the texel size, or the pitch is below a row");
    ENGINE_ASSERT(region.buffer_offset % caps.buffer_texture_copy_offset_alignment == 0 && pitch % caps.buffer_texture_copy_row_pitch_alignment == 0,
                  "CommandList copy: buffer_offset or the row pitch breaks the device's copy alignment (DeviceCaps)");
    const u64 block_rows = (static_cast<u64>(height) + fp.block_height - 1) / fp.block_height;
    const u64 needed = (depth * block_rows - 1) * pitch + row_bytes;
    ENGINE_ASSERT(region.buffer_offset <= b.desc.size && needed <= b.desc.size - region.buffer_offset,
                  "CommandList copy: the buffer range is too small for the level");
    if (!usable) {
        return;
    }
    const D3D12_RESOURCE_DESC rd = t.resource->GetDesc();
    const UINT layer = t.desc.dimension == TextureDimension::Tex3D ? 0 : region.array_layer;
    const UINT sub = ListSubresource(region.mip_level, layer, t.desc.mip_levels);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    device_->D3D()->GetCopyableFootprints(&rd, sub, 1, 0, &footprint, nullptr, nullptr, nullptr);
    footprint.Offset = region.buffer_offset;
    footprint.Footprint.RowPitch = static_cast<UINT>(pitch);
    D3D12_TEXTURE_COPY_LOCATION tex{};
    tex.pResource = t.resource;
    tex.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    tex.SubresourceIndex = sub;
    D3D12_TEXTURE_COPY_LOCATION buf{};
    buf.pResource = b.resource;
    buf.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    buf.PlacedFootprint = footprint;
    if (b.desc.location == MemoryLocation::GpuOnly) UseBuffer(b.resource, to_texture ? D3D12_RESOURCE_STATE_COPY_SOURCE : D3D12_RESOURCE_STATE_COPY_DEST);
    if (to_texture) {
        list_->CopyTextureRegion(&tex, 0, 0, 0, &buf, nullptr);
    } else {
        list_->CopyTextureRegion(&buf, 0, 0, 0, &tex, nullptr);
    }
}

void Dx12CommandList::CopyBufferToTexture(BufferHandle src, TextureHandle dst, const BufferTextureCopy& region) { CopyLevel(src, dst, region, true); }

void Dx12CommandList::CopyTextureToBuffer(TextureHandle src, BufferHandle dst, const BufferTextureCopy& region) { CopyLevel(dst, src, region, false); }

// ---- barriers ----

void Dx12CommandList::Barrier(BufferHandle buffer, ResourceState before, ResourceState after) {
    const bool usable = Recording("CommandList::Barrier: the list is not recording");
    ENGINE_ASSERT(!in_rendering_, "CommandList::Barrier: not allowed inside a rendering pass");
    Dx12Device::BufferInfo b;
    ENGINE_ASSERT(device_->LookupBuffer(buffer, &b), "CommandList::Barrier: stale or invalid buffer handle");
    ENGINE_ASSERT(b.desc.location == MemoryLocation::GpuOnly, "CommandList::Barrier: only GpuOnly buffers take barriers");
    ENGINE_ASSERT(IsBufferState(before) && IsBufferState(after) && after != ResourceState::Undefined, "CommandList::Barrier: a state is not valid for a buffer");
    if (!usable) {
        return;
    }
    // `before` is not needed: the list knows the buffer's state (COMMON at its start, then what it did).
    const D3D12_RESOURCE_STATES target = ToD3D12State(after);
    BufferState* s = FindBuffer(b.resource);
    if (s->state == target) {
        if (target == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            const D3D12_RESOURCE_BARRIER u = UavBarrier(b.resource); // write after write: wait for the earlier writes
            list_->ResourceBarrier(1, &u);
        }
        return;
    }
    const D3D12_RESOURCE_BARRIER t = ListTransition(b.resource, s->state, target);
    list_->ResourceBarrier(1, &t);
    s->state = target;
    s->promoted = false;
}

void Dx12CommandList::Barrier(TextureHandle texture, ResourceState before, ResourceState after) {
    const bool usable = Recording("CommandList::Barrier: the list is not recording");
    ENGINE_ASSERT(!in_rendering_, "CommandList::Barrier: not allowed inside a rendering pass");
    Dx12Device::TextureInfo t;
    ENGINE_ASSERT(device_->LookupTexture(texture, &t), "CommandList::Barrier: stale or invalid texture handle");
    ENGINE_ASSERT(after != ResourceState::Undefined && before != ResourceState::Count && after != ResourceState::Count,
                  "CommandList::Barrier: a state is not valid for a texture");
    if (!usable) {
        return;
    }
    const D3D12_RESOURCE_STATES target = TextureStateFor(after, t.desc);
    TextureUse* u = FindTexture(t.resource);
    if (u == nullptr) {
        // The first barrier on this texture in the list. The texture's state when the list runs is the device's business: the list
        // records what it needs, and the device transitions the texture into it first (Submit). With `Undefined` it needs the state
        // after the barrier and the barrier itself happens there.
        TextureUse fresh;
        fresh.texture = texture;
        fresh.resource = t.resource;
        fresh.initial = before == ResourceState::Undefined ? target : TextureStateFor(before, t.desc);
        fresh.current = fresh.initial;
        textures_.Push(fresh);
        u = &textures_[textures_.Size() - 1];
    }
    if (u->current == target) {
        if (target == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            const D3D12_RESOURCE_BARRIER b = UavBarrier(t.resource);
            list_->ResourceBarrier(1, &b);
        }
        return;
    }
    const D3D12_RESOURCE_BARRIER b = ListTransition(t.resource, u->current, target);
    list_->ResourceBarrier(1, &b);
    u->current = target;
}

// ---- mips ----

void Dx12CommandList::GenerateMips(TextureHandle texture) {
    const bool usable = Recording("CommandList::GenerateMips: the list is not recording");
    ENGINE_ASSERT(!in_rendering_, "CommandList::GenerateMips: not allowed inside a rendering pass");
    Dx12Device::TextureInfo t;
    ENGINE_ASSERT(device_->LookupTexture(texture, &t), "CommandList::GenerateMips: stale or invalid texture handle");
    ENGINE_ASSERT(Has(t.desc.usage, TextureUsage::Sampled) && Has(t.desc.usage, TextureUsage::CopySrc) && Has(t.desc.usage, TextureUsage::CopyDst),
                  "CommandList::GenerateMips: the texture needs Sampled, CopySrc and CopyDst usage");
    ENGINE_ASSERT(t.desc.mip_levels > 1, "CommandList::GenerateMips: the texture has a single mip level");
    ENGINE_ASSERT(device_->MipGenCapable(t.desc), "CommandList::GenerateMips: Direct3D 12 cannot generate this texture's mips (a 3D, multisampled or "
                                                 "depth texture, or a format without typed storage; check Device::CanGenerateMips)");
    if (!usable) {
        return;
    }
    Dx12Device::MipPipeline mip;
    if (!device_->MipPipelineFor(&mip)) {
        Fail("GenerateMips (the mip pipeline)", E_FAIL);
        return;
    }
    list_->SetComputeRootSignature(mip.root);
    list_->SetPipelineState(mip.state);
    pipeline_applied_ = false; // the bound pipeline is set again before the next draw or dispatch
    applied_state_ = nullptr;
    const u32 levels = t.desc.mip_levels;
    const u32 layers = t.desc.array_layers;
    ID3D12Device* d3d = device_->D3D();
    const u32 inc = d3d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    Vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.Resize(layers);
    const u32 srgb = FormatInfo(t.desc.format).is_srgb ? 1 : 0;
    for (u32 level = 1; level < levels; ++level) {
        for (u32 l = 0; l < layers; ++l) {
            barriers[l] = ListTransition(t.resource, kShaderResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, ListSubresource(level, l, levels));
        }
        list_->ResourceBarrier(layers, barriers.Data());
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
        if (!device_->ViewRing().Allocate(static_cast<u32>(frame_ % Dx12Device::kFrameSlots), 2, &cpu, &gpu)) {
            RhiLog(LogVerbosity::Error, "D3D12: GenerateMips: the frame's shader-visible descriptors (%u) are used up", device_->ViewRing().PerSlot());
            Fail("GenerateMips", E_OUTOFMEMORY);
            return;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = ToDxgiShaderView(t.desc.format); // an sRGB view decodes, so the filter works on linear values
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2DArray.MostDetailedMip = level - 1;
        sv.Texture2DArray.MipLevels = 1;
        sv.Texture2DArray.ArraySize = layers;
        d3d->CreateShaderResourceView(t.resource, &sv, cpu);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};
        uv.Format = Dx12Device::MipStorageFormat(t.desc.format); // linear: the shader encodes sRGB itself
        uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uv.Texture2DArray.MipSlice = level;
        uv.Texture2DArray.ArraySize = layers;
        d3d->CreateUnorderedAccessView(t.resource, nullptr, &uv, D3D12_CPU_DESCRIPTOR_HANDLE{cpu.ptr + inc});
        const u32 w = LevelExtent(t.desc.width, level), h = LevelExtent(t.desc.height, level);
        const u32 constants[4] = {w, h, srgb, 0};
        list_->SetComputeRootDescriptorTable(0, gpu);
        list_->SetComputeRoot32BitConstants(1, 4, constants, 0);
        list_->Dispatch((w + 7) / 8, (h + 7) / 8, layers);
        for (u32 l = 0; l < layers; ++l) {
            barriers[l] = ListTransition(t.resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kShaderResource, ListSubresource(level, l, levels));
        }
        list_->ResourceBarrier(layers, barriers.Data());
    }
}

// ---- rendering ----

void Dx12CommandList::BeginRendering(const RenderingDesc& desc) {
    const bool usable = Recording("CommandList::BeginRendering: the list is not recording");
    ENGINE_ASSERT(!in_rendering_, "CommandList::BeginRendering: a rendering pass is already open");
    ENGINE_ASSERT(desc.color_count <= kMaxColorAttachments, "CommandList::BeginRendering: too many color attachments");
    ENGINE_ASSERT(desc.color_count > 0 || desc.depth.texture.IsValid(), "CommandList::BeginRendering: no attachments");
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[kMaxColorAttachments]{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    ID3D12Resource* color_resources[kMaxColorAttachments] = {};
    u32 width = 0, height = 0;
    auto note_extent = [&](const Dx12Device::TextureInfo& t) {
        if (width == 0) {
            width = t.desc.width;
            height = t.desc.height;
        }
        ENGINE_ASSERT(t.desc.width == width && t.desc.height == height, "CommandList::BeginRendering: attachment extents differ");
    };
    pass_resource_count_ = 0;
    for (u32 i = 0; i < desc.color_count; ++i) {
        Dx12Device::TextureInfo t;
        ENGINE_ASSERT(device_->LookupTexture(desc.color[i].texture, &t), "CommandList::BeginRendering: stale or invalid color attachment");
        ENGINE_ASSERT(Has(t.desc.usage, TextureUsage::ColorAttachment) && t.rtv.ptr != 0, "CommandList::BeginRendering: a color attachment lacks ColorAttachment usage");
        note_extent(t);
        pass_color_formats_[i] = t.desc.format;
        rtvs[i] = t.rtv;
        color_resources[i] = t.resource;
        // A color attachment is in RENDER_TARGET (the RHI's rule), where DiscardResource is allowed.
        pass_resources_[pass_resource_count_] = t.resource;
        pass_discard_[pass_resource_count_++] = desc.color[i].store == StoreOp::DontCare;
    }
    bool has_stencil = false;
    if (desc.depth.texture.IsValid()) {
        Dx12Device::TextureInfo t;
        ENGINE_ASSERT(device_->LookupTexture(desc.depth.texture, &t), "CommandList::BeginRendering: stale or invalid depth attachment");
        ENGINE_ASSERT(Has(t.desc.usage, TextureUsage::DepthAttachment) && t.dsv.ptr != 0, "CommandList::BeginRendering: the depth attachment lacks DepthAttachment usage");
        note_extent(t);
        pass_depth_format_ = t.desc.format;
        dsv = t.dsv;
        has_stencil = FormatInfo(t.desc.format).has_stencil;
    } else {
        pass_depth_format_ = Format::Undefined;
    }
    in_rendering_ = true;
    pass_color_count_ = desc.color_count;
    if (pipeline_bound_ && !pipeline_.compute) {
        pipeline_bound_ = false; // a graphics pipeline binding ends with the pass
    }
    index_bound_ = false;
    if (!usable) {
        return;
    }
    list_->OMSetRenderTargets(desc.color_count, desc.color_count != 0 ? rtvs : nullptr, FALSE, dsv.ptr != 0 ? &dsv : nullptr);
    for (u32 i = 0; i < desc.color_count; ++i) {
        if (desc.color[i].load == LoadOp::Clear) {
            list_->ClearRenderTargetView(rtvs[i], desc.color[i].clear_color, 0, nullptr);
        } else if (desc.color[i].load == LoadOp::DontCare) {
            list_->DiscardResource(color_resources[i], &kAttachmentRegion);
        }
    }
    // The depth attachment may be read-only (DEPTH_READ), where a discard is not allowed: a don't-care depth is left as it is.
    if (dsv.ptr != 0 && desc.depth.load == LoadOp::Clear) {
        const D3D12_CLEAR_FLAGS flags = has_stencil ? (D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL) : D3D12_CLEAR_FLAG_DEPTH;
        list_->ClearDepthStencilView(dsv, flags, desc.depth.clear_depth, 0, 0, nullptr);
    }
    // The pass's extent is the default viewport and scissor, as on Vulkan.
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<f32>(width), static_cast<f32>(height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list_->RSSetViewports(1, &viewport);
    list_->RSSetScissorRects(1, &scissor);
}

void Dx12CommandList::EndRendering() {
    const bool usable = Recording("CommandList::EndRendering: the list is not recording");
    ENGINE_ASSERT(in_rendering_, "CommandList::EndRendering: no rendering pass is open");
    in_rendering_ = false;
    if (pipeline_bound_ && !pipeline_.compute) pipeline_bound_ = false;
    if (usable) {
        for (u32 i = 0; i < pass_resource_count_; ++i) {
            if (pass_discard_[i]) list_->DiscardResource(pass_resources_[i], &kAttachmentRegion);
        }
    }
    pass_resource_count_ = 0;
}

void Dx12CommandList::PushDebugLabel(const char* name) {
    const bool usable = Recording("CommandList::PushDebugLabel: the list is not recording");
    ENGINE_ASSERT(name != nullptr, "CommandList::PushDebugLabel: name is null");
    ++label_depth_;
    if (usable) {
        // Metadata 1 is PIX's ANSI string (WINPIX_EVENT_ANSI_VERSION): tools that read the plain BeginEvent show it.
        list_->BeginEvent(1, name, static_cast<UINT>(std::strlen(name) + 1));
    }
}

void Dx12CommandList::PopDebugLabel() {
    const bool usable = Recording("CommandList::PopDebugLabel: the list is not recording");
    ENGINE_ASSERT(label_depth_ > 0, "CommandList::PopDebugLabel: no label is open");
    --label_depth_;
    if (usable) list_->EndEvent();
}

// ---- pipelines and bindings ----

void Dx12CommandList::SetPipeline(PipelineHandle pipeline) {
    const bool usable = Recording("CommandList::SetPipeline: the list is not recording");
    Dx12Device::PipelineInfo info;
    ENGINE_ASSERT(device_->LookupPipeline(pipeline, &info), "CommandList::SetPipeline: stale or invalid pipeline handle");
    if (info.compute) {
        ENGINE_ASSERT(!in_rendering_, "CommandList::SetPipeline: a compute pipeline cannot be bound inside a rendering pass");
    } else {
        ENGINE_ASSERT(in_rendering_, "CommandList::SetPipeline: a graphics pipeline can only be bound inside a rendering pass");
        bool formats_match = info.color_count == pass_color_count_ && info.depth_format == pass_depth_format_;
        for (u32 i = 0; formats_match && i < info.color_count; ++i) {
            formats_match = info.color_formats[i] == pass_color_formats_[i];
        }
        ENGINE_ASSERT(formats_match, "CommandList::SetPipeline: the pipeline's attachment formats differ from the rendering pass's");
    }
    pipeline_ = info;
    pipeline_bound_ = true;
    push_written_ = 0;
    if (usable) ApplyPipeline();
}

// Sets the bound pipeline's root signature, state and topology on the list (again after GenerateMips used its own), and everything
// that goes with a root signature: the groups and the push constants.
void Dx12CommandList::ApplyPipeline() {
    if (pipeline_.compute) {
        list_->SetComputeRootSignature(pipeline_.root.signature);
    } else {
        list_->SetGraphicsRootSignature(pipeline_.root.signature);
        list_->IASetPrimitiveTopology(pipeline_.topology);
    }
    list_->SetPipelineState(pipeline_.state);
    applied_state_ = pipeline_.state;
    pipeline_applied_ = true;
    groups_dirty_ = (1u << kMaxBindGroups) - 1;
    vertex_dirty_ = true;
    if (push_written_ != 0 && pipeline_.root.push_param >= 0) {
        if (pipeline_.compute) {
            list_->SetComputeRoot32BitConstants(static_cast<UINT>(pipeline_.root.push_param), push_written_ / 4, push_shadow_, 0);
        } else {
            list_->SetGraphicsRoot32BitConstants(static_cast<UINT>(pipeline_.root.push_param), push_written_ / 4, push_shadow_, 0);
        }
    }
}

void Dx12CommandList::SetBindGroup(u32 slot, BindGroupHandle group) {
    Recording("CommandList::SetBindGroup: the list is not recording");
    ENGINE_ASSERT(slot < device_->Caps().max_bind_groups, "CommandList::SetBindGroup: slot is outside the device limit");
    Dx12Device::BindGroupInfo info;
    ENGINE_ASSERT(device_->LookupBindGroup(group, &info), "CommandList::SetBindGroup: stale or invalid bind group handle");
    ENGINE_ASSERT(info.resources_live, "CommandList::SetBindGroup: a resource the bind group refers to was destroyed");
    ENGINE_ASSERT(!info.transient || info.frame == frame_, "CommandList::SetBindGroup: a transient bind group is only valid in the frame it was created in");
    groups_[slot] = info;
    group_set_[slot] = true;
    groups_dirty_ |= 1u << slot;
}

bool Dx12CommandList::FlushBindGroups(const char* what) {
    if (!pipeline_applied_) ApplyPipeline();
    const Dx12Device::RootLayout& root = pipeline_.root;
    for (u32 slot = 0; slot < root.layout_count; ++slot) {
        ENGINE_ASSERT(group_set_[slot], what);
        if ((groups_dirty_ & (1u << slot)) == 0) continue;
        const Dx12Device::BindGroupInfo& g = groups_[slot];
        ENGINE_ASSERT(g.layout_id == root.layout_ids[slot], "the bind group's layout differs from the pipeline's layout for that slot");
        if (g.view_count != 0 && root.view_param[slot] >= 0) {
            D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
            D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
            if (!device_->ViewRing().Allocate(static_cast<u32>(frame_ % Dx12Device::kFrameSlots), g.view_count, &cpu, &gpu)) {
                RhiLog(LogVerbosity::Error, "D3D12: SetBindGroup: the frame's %u shader-visible descriptors are used up (%u more wanted)",
                       device_->ViewRing().PerSlot(), g.view_count);
                Fail("SetBindGroup", E_OUTOFMEMORY);
                return false;
            }
            device_->D3D()->CopyDescriptorsSimple(g.view_count, cpu, g.views, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            if (pipeline_.compute) {
                list_->SetComputeRootDescriptorTable(static_cast<UINT>(root.view_param[slot]), gpu);
            } else {
                list_->SetGraphicsRootDescriptorTable(static_cast<UINT>(root.view_param[slot]), gpu);
            }
        }
        if (g.sampler_count != 0 && root.sampler_param[slot] >= 0) {
            if (pipeline_.compute) {
                list_->SetComputeRootDescriptorTable(static_cast<UINT>(root.sampler_param[slot]), g.samplers);
            } else {
                list_->SetGraphicsRootDescriptorTable(static_cast<UINT>(root.sampler_param[slot]), g.samplers);
            }
        }
        for (u32 i = 0; i < g.buffer_use_count; ++i) {
            UseBuffer(g.buffer_uses[i].resource, g.buffer_uses[i].state);
        }
    }
    groups_dirty_ = 0;
    return true;
}

void Dx12CommandList::SetPushConstants(u32 offset, const void* data, u32 size) {
    const bool usable = Recording("CommandList::SetPushConstants: the list is not recording");
    ENGINE_ASSERT(pipeline_bound_, "CommandList::SetPushConstants: no pipeline is bound");
    ENGINE_ASSERT(data != nullptr && offset % 4 == 0 && size % 4 == 0 && size > 0,
                  "CommandList::SetPushConstants: offset and size must be non-zero multiples of 4 and data must not be null");
    ENGINE_ASSERT(offset <= pipeline_.push_bytes && size <= pipeline_.push_bytes - offset, "CommandList::SetPushConstants: the range exceeds the pipeline's push-constant range");
    std::memcpy(push_shadow_ + offset, data, size);
    if (offset + size > push_written_) push_written_ = offset + size;
    if (!usable || !pipeline_applied_ || pipeline_.root.push_param < 0) {
        return; // set with the pipeline when it is applied again
    }
    if (pipeline_.compute) {
        list_->SetComputeRoot32BitConstants(static_cast<UINT>(pipeline_.root.push_param), size / 4, data, offset / 4);
    } else {
        list_->SetGraphicsRoot32BitConstants(static_cast<UINT>(pipeline_.root.push_param), size / 4, data, offset / 4);
    }
}

void Dx12CommandList::SetViewport(const Viewport& viewport) {
    const bool usable = Recording("CommandList::SetViewport: the list is not recording");
    ENGINE_ASSERT(viewport.width > 0.0f && viewport.height > 0.0f, "CommandList::SetViewport: the viewport must have a positive size");
    if (usable) {
        // The same rectangle as on Vulkan: the D3D12 build of the shaders flips clip-space Y instead (shaders/target_common.slang).
        const D3D12_VIEWPORT v{viewport.x, viewport.y, viewport.width, viewport.height, viewport.min_depth, viewport.max_depth};
        list_->RSSetViewports(1, &v);
    }
}

void Dx12CommandList::SetScissor(const ScissorRect& scissor) {
    const bool usable = Recording("CommandList::SetScissor: the list is not recording");
    if (usable) {
        const D3D12_RECT r{scissor.x, scissor.y, static_cast<LONG>(scissor.x + static_cast<i64>(scissor.width)),
                           static_cast<LONG>(scissor.y + static_cast<i64>(scissor.height))};
        list_->RSSetScissorRects(1, &r);
    }
}

void Dx12CommandList::SetVertexBuffer(u32 slot, BufferHandle buffer, u64 offset) {
    const bool usable = Recording("CommandList::SetVertexBuffer: the list is not recording");
    ENGINE_ASSERT(slot < kMaxVertexBindings, "CommandList::SetVertexBuffer: slot out of range");
    Dx12Device::BufferInfo b;
    ENGINE_ASSERT(device_->LookupBuffer(buffer, &b), "CommandList::SetVertexBuffer: stale or invalid buffer handle");
    ENGINE_ASSERT(Has(b.desc.usage, BufferUsage::Vertex), "CommandList::SetVertexBuffer: the buffer lacks Vertex usage");
    ENGINE_ASSERT(offset < b.desc.size, "CommandList::SetVertexBuffer: the offset is past the end of the buffer");
    if (!usable) {
        return;
    }
    vertex_slots_[slot] = VertexSlot{b.gpu_address + offset, b.desc.size - offset};
    if (slot + 1 > vertex_slot_count_) vertex_slot_count_ = slot + 1;
    vertex_dirty_ = true; // set at the draw, with the pipeline's strides
    if (b.desc.location == MemoryLocation::GpuOnly) UseBuffer(b.resource, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
}

void Dx12CommandList::SetIndexBuffer(BufferHandle buffer, u64 offset, IndexType type) {
    const bool usable = Recording("CommandList::SetIndexBuffer: the list is not recording");
    Dx12Device::BufferInfo b;
    ENGINE_ASSERT(device_->LookupBuffer(buffer, &b), "CommandList::SetIndexBuffer: stale or invalid buffer handle");
    ENGINE_ASSERT(Has(b.desc.usage, BufferUsage::Index), "CommandList::SetIndexBuffer: the buffer lacks Index usage");
    const u64 index_size = type == IndexType::UInt16 ? 2 : 4;
    ENGINE_ASSERT(offset % index_size == 0 && offset < b.desc.size, "CommandList::SetIndexBuffer: misaligned offset or past the end");
    index_bound_ = true;
    index_type_ = type;
    if (!usable) {
        return;
    }
    D3D12_INDEX_BUFFER_VIEW v{};
    v.BufferLocation = b.gpu_address + offset;
    v.SizeInBytes = static_cast<UINT>(b.desc.size - offset);
    v.Format = type == IndexType::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    list_->IASetIndexBuffer(&v);
    if (b.desc.location == MemoryLocation::GpuOnly) UseBuffer(b.resource, D3D12_RESOURCE_STATE_INDEX_BUFFER);
}

void Dx12CommandList::FlushVertexBuffers() {
    if (!vertex_dirty_ || pipeline_.vertex_binding_count == 0) {
        vertex_dirty_ = false;
        return;
    }
    D3D12_VERTEX_BUFFER_VIEW views[kMaxVertexBindings]{};
    for (u32 i = 0; i < pipeline_.vertex_binding_count; ++i) {
        if (i < vertex_slot_count_) {
            views[i].BufferLocation = vertex_slots_[i].address;
            views[i].SizeInBytes = static_cast<UINT>(vertex_slots_[i].size);
        }
        views[i].StrideInBytes = pipeline_.vertex_strides[i];
    }
    list_->IASetVertexBuffers(0, pipeline_.vertex_binding_count, views);
    vertex_dirty_ = false;
}

// A strip with primitive restart has a state per index size (D3D12 fixes the cut value in the state).
void Dx12CommandList::FlushIndexedState(bool indexed) {
    ID3D12PipelineState* want = indexed && index_type_ == IndexType::UInt16 && pipeline_.state_cut16 != nullptr ? pipeline_.state_cut16 : pipeline_.state;
    if (want != applied_state_) {
        list_->SetPipelineState(want);
        applied_state_ = want;
    }
}

void Dx12CommandList::Draw(u32 vertex_count, u32 instance_count, u32 first_vertex, u32 first_instance) {
    const bool usable = Recording("CommandList::Draw: the list is not recording");
    ENGINE_ASSERT(in_rendering_ && pipeline_bound_ && !pipeline_.compute, "CommandList::Draw: needs a rendering pass with a graphics pipeline bound");
    if (!usable || !FlushBindGroups("CommandList::Draw: the pipeline declares a bind group for a slot that has none")) {
        return;
    }
    FlushVertexBuffers();
    FlushIndexedState(false);
    // first_instance offsets SV_InstanceID's reads of an instance-rate buffer, so the object index stream works (shaders/target_common.slang).
    list_->DrawInstanced(vertex_count, instance_count, first_vertex, first_instance);
}

void Dx12CommandList::DrawIndexed(u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset, u32 first_instance) {
    const bool usable = Recording("CommandList::DrawIndexed: the list is not recording");
    ENGINE_ASSERT(in_rendering_ && pipeline_bound_ && !pipeline_.compute, "CommandList::DrawIndexed: needs a rendering pass with a graphics pipeline bound");
    ENGINE_ASSERT(index_bound_, "CommandList::DrawIndexed: no index buffer is bound");
    if (!usable || !FlushBindGroups("CommandList::DrawIndexed: the pipeline declares a bind group for a slot that has none")) {
        return;
    }
    FlushVertexBuffers();
    FlushIndexedState(true);
    list_->DrawIndexedInstanced(index_count, instance_count, first_index, vertex_offset, first_instance);
}

void Dx12CommandList::Dispatch(u32 x, u32 y, u32 z) {
    const bool usable = Recording("CommandList::Dispatch: the list is not recording");
    ENGINE_ASSERT(!in_rendering_, "CommandList::Dispatch: not allowed inside a rendering pass");
    ENGINE_ASSERT(pipeline_bound_ && pipeline_.compute, "CommandList::Dispatch: no compute pipeline is bound");
    const DeviceCaps& caps = device_->Caps();
    ENGINE_ASSERT(x <= caps.max_compute_work_group_count[0] && y <= caps.max_compute_work_group_count[1] && z <= caps.max_compute_work_group_count[2],
                  "CommandList::Dispatch: a workgroup count exceeds the device limit");
    if (!usable || !FlushBindGroups("CommandList::Dispatch: the pipeline declares a bind group for a slot that has none")) {
        return;
    }
    list_->Dispatch(x, y, z);
}

} // namespace engine_placeholder::rhi::dx12
