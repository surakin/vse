// Dx12Device: buffers, textures, their views, the staging ring, and uploads and readbacks (Phase 13 Day 165).
//
// Resources are committed (one heap each, decision 10). Buffers live on the default heap (GpuOnly), the upload heap (CpuToGpu, mapped
// for their whole life, fixed state GENERIC_READ) or the readback heap (GpuToCpu, mapped, fixed state COPY_DEST). A buffer with Uniform
// usage is made a multiple of 256 bytes, so every CBV range over it can be one. Textures start in COMMON; a sampled depth texture is
// typeless so that its depth view and its shader view can both exist. Copies between buffers and textures go through 256-byte row pitches
// (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) and 512-byte placements, repacked on the CPU from and to the caller's rows.

#include <cstdio>
#include <cstring>

#include "log/assert.h"
#include "os/windows/utf16.h"
#include "rhi/dx12/dx12_device.h"
#include "rhi/dx12/dx12_format.h"
#include "rhi/dx12/dx12_result.h"
#include "rhi/rhi_log.h"

namespace engine_placeholder::rhi::dx12 {

namespace {

constexpr u64 kStagingBytes = 64ull << 20;      // the upload ring
constexpr u64 kUploadChunkBytes = 16ull << 20;  // the largest part one upload submission stages
constexpr u64 kStagingWaitNs = 5ull * 1000 * 1000 * 1000;

u64 AlignUp(u64 value, u64 alignment) { return (value + alignment - 1) / alignment * alignment; }

u32 MipExtent(u32 base, u32 mip) {
    const u32 e = base >> mip;
    return e == 0 ? 1 : e;
}

D3D12_HEAP_TYPE HeapOf(MemoryLocation location) {
    switch (location) {
    case MemoryLocation::GpuOnly: return D3D12_HEAP_TYPE_DEFAULT;
    case MemoryLocation::CpuToGpu: return D3D12_HEAP_TYPE_UPLOAD;
    case MemoryLocation::GpuToCpu: return D3D12_HEAP_TYPE_READBACK;
    }
    return D3D12_HEAP_TYPE_DEFAULT;
}

// The fixed state of a buffer on an upload or readback heap, or COMMON for the default heap (buffers decay to it after every list).
D3D12_RESOURCE_STATES InitialBufferState(MemoryLocation location) {
    switch (location) {
    case MemoryLocation::GpuOnly: return D3D12_RESOURCE_STATE_COMMON;
    case MemoryLocation::CpuToGpu: return D3D12_RESOURCE_STATE_GENERIC_READ;
    case MemoryLocation::GpuToCpu: return D3D12_RESOURCE_STATE_COPY_DEST;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

D3D12_RESOURCE_DESC BufferResourceDesc(u64 size, D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = size;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = DXGI_FORMAT_UNKNOWN;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = flags;
    return d;
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    return b;
}

constexpr D3D12_RESOURCE_STATES kShaderRead = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

} // namespace

// ---- setup ----

bool Dx12Device::InitializeResources() {
    const u32 seed = GenerationSeed(epoch_);
    buffers_.SeedGeneration(seed);
    textures_.SeedGeneration(seed);
    view_heap_.Initialize(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    rtv_heap_.Initialize(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    dsv_heap_.Initialize(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    FillFormatSupport();
    if (!CreateCommitted(D3D12_HEAP_TYPE_UPLOAD, BufferResourceDesc(kStagingBytes, D3D12_RESOURCE_FLAG_NONE), D3D12_RESOURCE_STATE_GENERIC_READ,
                         "engine staging ring", &staging_, &staging_bytes_)) {
        return false;
    }
    D3D12_RANGE none{0, 0}; // the CPU never reads it
    if (!Check(staging_->Map(0, &none, reinterpret_cast<void**>(&staging_mapped_)), "Map (staging ring)")) {
        return false;
    }
    staging_ring_ = MakeUnique<RingAllocator>(kStagingBytes);
    return staging_ring_.Get() != nullptr;
}

void Dx12Device::ShutdownResources() {
    // Every user resource is collected by now (the destructor's WaitIdle and collection); what is left was leaked by the application
    // and is released here with the device.
    buffers_.ForEachLive([&](BufferHandle, BufferRes& b) {
        ReleaseResourceBytes(b.bytes);
        b.resource.Reset();
    });
    textures_.ForEachLive([&](TextureHandle, TextureRes& t) {
        ReleaseTextureViews(t);
        if (!t.external) ReleaseResourceBytes(t.bytes);
        t.resource.Reset();
    });
    internal_lists_.Clear();
    staging_ring_.Reset();
    if (staging_) {
        staging_->Unmap(0, nullptr);
        ReleaseResourceBytes(staging_bytes_); // the size D3D12 reported at creation, as counted
        staging_.Reset();
    }
    view_heap_.Shutdown();
    rtv_heap_.Shutdown();
    dsv_heap_.Shutdown();
}

void Dx12Device::FillFormatSupport() {
    for (usize i = 1; i < static_cast<usize>(Format::Count); ++i) {
        const Format f = static_cast<Format>(i);
        // A depth format is asked about as itself (depth-stencil) and through its shader view (sampling): both bits are merged.
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
        support.Format = ToDxgi(f);
        u32 bits = 0;
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)))) {
            bits = static_cast<u32>(support.Support1);
        }
        if (FormatInfo(f).is_depth) {
            D3D12_FEATURE_DATA_FORMAT_SUPPORT view{};
            view.Format = ToDxgiShaderView(f);
            if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &view, sizeof(view)))) {
                bits |= static_cast<u32>(view.Support1) & (D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE | D3D12_FORMAT_SUPPORT1_SHADER_LOAD);
            }
        }
        format_support_[i] = bits;
    }
    format_support_ready_ = true;
}

bool Dx12Device::FormatSupport(Format format, TextureUsage usage) const {
    if (format == Format::Undefined || static_cast<usize>(format) >= static_cast<usize>(Format::Count) || !format_support_ready_) {
        return false;
    }
    const u32 bits = format_support_[static_cast<usize>(format)];
    if ((bits & D3D12_FORMAT_SUPPORT1_TEXTURE2D) == 0) {
        return false;
    }
    // Sampled means a shader can read it (Vulkan's SAMPLED_IMAGE): a load is enough, so integer formats, which D3D12 loads but never
    // filters, count as sampled as they do on Vulkan.
    if (Has(usage, TextureUsage::Sampled) && (bits & D3D12_FORMAT_SUPPORT1_SHADER_LOAD) == 0) {
        return false;
    }
    if (Has(usage, TextureUsage::Storage) && (bits & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) == 0) {
        return false;
    }
    if (Has(usage, TextureUsage::ColorAttachment) && (bits & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) == 0) {
        return false;
    }
    if (Has(usage, TextureUsage::DepthAttachment) && (bits & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL) == 0) {
        return false;
    }
    return true;
}

bool Dx12Device::CanGenerateMips(Format) const {
    return false; // the compute-shader mip path arrives on Day 168
}

void Dx12Device::SetDebugName(ID3D12Object* object, const char* name) {
    if (object == nullptr || name == nullptr || name[0] == '\0') {
        return;
    }
    wchar_t wide[128];
    const usize n = os::windows::Utf8ToUtf16(StringView(name), wide, 128);
    wide[n < 128 ? n : 127] = L'\0';
    object->SetName(wide);
}

bool Dx12Device::CreateCommitted(D3D12_HEAP_TYPE heap, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES state, const char* name,
                                 ComRef<ID3D12Resource>* out, u64* bytes) {
    D3D12_HEAP_PROPERTIES props{};
    props.Type = heap;
    const HRESULT hr = device_->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, __uuidof(ID3D12Resource), out->PutVoid());
    if (FAILED(hr)) {
        if (IsDeviceLostResult(hr)) {
            NoteRemoved("CreateCommittedResource");
        } else {
            RhiLog(LogVerbosity::Error, "D3D12: cannot create the resource '%s' (%s, 0x%08lx)", name != nullptr ? name : "(unnamed)", HresultName(hr),
                   static_cast<unsigned long>(hr));
        }
        return false;
    }
    SetDebugName(out->Get(), name);
    const D3D12_RESOURCE_ALLOCATION_INFO info = device_->GetResourceAllocationInfo(0, 1, &desc);
    *bytes = info.SizeInBytes;
    device_memory_bytes_.fetch_add(info.SizeInBytes, std::memory_order_relaxed);
    raw_resources_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void Dx12Device::ReleaseResourceBytes(u64 bytes) {
    device_memory_bytes_.fetch_sub(bytes, std::memory_order_relaxed);
    raw_resources_.fetch_sub(1, std::memory_order_relaxed);
}

void Dx12Device::ReleaseTextureViews(TextureRes& t) {
    view_heap_.Free(t.srv);
    view_heap_.Free(t.uav);
    rtv_heap_.Free(t.rtv);
    dsv_heap_.Free(t.dsv);
    t.srv = t.uav = t.rtv = t.dsv = D3D12_CPU_DESCRIPTOR_HANDLE{};
}

// ---- internal submissions ----

ID3D12GraphicsCommandList* Dx12Device::BeginInternalLocked(usize* slot) {
    if (device_lost_.load(std::memory_order_relaxed)) {
        return nullptr;
    }
    const u64 completed = CompletedFence();
    for (usize i = 0; i < internal_lists_.Size(); ++i) {
        InternalList& e = internal_lists_[i];
        if (e.fence <= completed) {
            if (!Check(e.allocator->Reset(), "ID3D12CommandAllocator::Reset (internal)") ||
                !Check(e.list->Reset(e.allocator.Get(), nullptr), "ID3D12GraphicsCommandList::Reset (internal)")) {
                return nullptr;
            }
            e.fence = ~0ull;
            *slot = i;
            return e.list.Get();
        }
    }
    InternalList fresh;
    if (!Check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), fresh.allocator.PutVoid()),
               "CreateCommandAllocator (internal)") ||
        !Check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, fresh.allocator.Get(), nullptr, __uuidof(ID3D12GraphicsCommandList),
                                          fresh.list.PutVoid()),
               "CreateCommandList (internal)")) {
        return nullptr;
    }
    fresh.list->SetName(L"engine internal upload list");
    fresh.fence = ~0ull;
    internal_lists_.Push(static_cast<InternalList&&>(fresh));
    *slot = internal_lists_.Size() - 1;
    return internal_lists_[*slot].list.Get();
}

u64 Dx12Device::SubmitInternalLocked(usize slot) {
    InternalList& e = internal_lists_[slot];
    if (!Check(e.list->Close(), "ID3D12GraphicsCommandList::Close (internal)")) {
        e.fence = 0;
        return 0;
    }
    ID3D12CommandList* lists[] = {e.list.Get()};
    queue_->ExecuteCommandLists(1, lists);
    const u64 value = SignalLocked();
    e.fence = value == 0 ? 0 : value;
    return value;
}

bool Dx12Device::StageLocked(u64 size, u64 alignment, u64* offset) {
    staging_ring_->Reclaim(CompletedFence());
    while (!staging_ring_->Allocate(size, alignment, offset)) {
        const u64 pending = staging_ring_->OldestPendingFence();
        if (pending == 0 || !WaitForValue(pending, kStagingWaitNs)) {
            RhiLog(LogVerbosity::Error, "D3D12: the staging ring is exhausted and the GPU is not freeing it");
            return false;
        }
        staging_ring_->Reclaim(CompletedFence());
    }
    return true;
}

// ---- buffers ----

BufferHandle Dx12Device::CreateBuffer(const BufferDesc& desc) {
    if (const char* why = desc.Validate()) {
        RhiLog(LogVerbosity::Error, "CreateBuffer: %s", why);
        return {};
    }
    BufferRes res;
    res.desc = desc;
    res.desc.debug_name = nullptr;
    // A CBV covers a multiple of 256 bytes (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT): the resource is rounded up so the last range fits.
    const u64 size = Has(desc.usage, BufferUsage::Uniform) ? AlignUp(desc.size, 256) : desc.size;
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (Has(desc.usage, BufferUsage::Storage) && desc.location == MemoryLocation::GpuOnly) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS; // an upload or readback heap cannot hold an unordered-access view
    }
    if (!CreateCommitted(HeapOf(desc.location), BufferResourceDesc(size, flags), InitialBufferState(desc.location), desc.debug_name, &res.resource,
                         &res.bytes)) {
        return {};
    }
    if (desc.location != MemoryLocation::GpuOnly) {
        // Mapped once, for the buffer's life (D3D12 allows persistent maps); MapBuffer hands out this pointer.
        D3D12_RANGE none{0, 0};
        const D3D12_RANGE* read = desc.location == MemoryLocation::GpuToCpu ? nullptr : &none;
        if (!Check(res.resource->Map(0, read, reinterpret_cast<void**>(&res.mapped)), "Map (buffer)")) {
            ReleaseResourceBytes(res.bytes);
            return {};
        }
    }
    std::lock_guard<std::mutex> lock(resource_mutex_);
    const BufferHandle handle = buffers_.Allocate(static_cast<BufferRes&&>(res));
    if (!handle.IsValid()) {
        RhiLog(LogVerbosity::Error, "CreateBuffer: out of buffer handles");
        ReleaseResourceBytes(res.bytes);
    }
    return handle;
}

void Dx12Device::DestroyBuffer(BufferHandle buffer) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    BufferRes* b = buffers_.Get(buffer);
    ENGINE_ASSERT(b != nullptr, "DestroyBuffer: handle is invalid or already destroyed");
    if (b == nullptr) {
        return;
    }
    ENGINE_ASSERT(!b->user_mapped, "DestroyBuffer: the buffer is still mapped");
    // Work in the open frame may still use it; outside a frame, the next frame is the earliest that can have used it.
    buffers_.Retire(buffer, frame_open_.load(std::memory_order_relaxed) ? frame_number_ : frame_number_ + 1);
}

bool Dx12Device::LookupBuffer(BufferHandle buffer, BufferInfo* out) const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    const BufferRes* b = buffers_.Get(buffer);
    if (b == nullptr) {
        return false;
    }
    out->resource = b->resource.Get();
    out->gpu_address = b->resource->GetGPUVirtualAddress();
    out->desc = b->desc;
    return true;
}

void* Dx12Device::MapBuffer(BufferHandle buffer) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    BufferRes* b = buffers_.Get(buffer);
    ENGINE_ASSERT(b != nullptr, "MapBuffer: handle is invalid or already destroyed");
    if (b == nullptr) {
        return nullptr;
    }
    ENGINE_ASSERT(b->desc.location != MemoryLocation::GpuOnly, "MapBuffer: the buffer is not CPU-visible");
    ENGINE_ASSERT(!b->user_mapped, "MapBuffer: the buffer is already mapped");
    b->user_mapped = true;
    return b->mapped;
}

void Dx12Device::UnmapBuffer(BufferHandle buffer) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    BufferRes* b = buffers_.Get(buffer);
    ENGINE_ASSERT(b != nullptr, "UnmapBuffer: handle is invalid or already destroyed");
    if (b == nullptr) {
        return;
    }
    ENGINE_ASSERT(b->user_mapped, "UnmapBuffer: the buffer is not mapped");
    b->user_mapped = false; // the persistent mapping stays; D3D12's upload and readback heaps are coherent, so nothing is flushed
}

UploadTicket Dx12Device::UploadBuffer(BufferHandle dst, u64 offset, const void* data, u64 size) {
    ID3D12Resource* resource = nullptr;
    BufferRes snapshot;
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        BufferRes* b = buffers_.Get(dst);
        ENGINE_ASSERT(b != nullptr, "UploadBuffer: handle is invalid or already destroyed");
        if (b == nullptr) {
            return UploadTicket{0, false};
        }
        ENGINE_ASSERT(Has(b->desc.usage, BufferUsage::CopyDst), "UploadBuffer: the buffer was not created with CopyDst usage");
        ENGINE_ASSERT(data != nullptr, "UploadBuffer: data is null");
        ENGINE_ASSERT(size <= b->desc.size && offset <= b->desc.size - size, "UploadBuffer: range outside the buffer");
        resource = b->resource.Get();
        snapshot.desc = b->desc;
        snapshot.mapped = b->mapped;
    }
    if (size == 0) {
        return {};
    }
    if (snapshot.desc.location == MemoryLocation::CpuToGpu) {
        std::memcpy(snapshot.mapped + offset, data, static_cast<usize>(size)); // coherent: visible to work submitted afterwards
        return {};
    }
    UploadTicket ticket;
    u64 done = 0;
    while (done < size) {
        const u64 chunk = size - done < kUploadChunkBytes ? size - done : kUploadChunkBytes;
        std::lock_guard<std::mutex> lock(submit_mutex_);
        u64 ring_offset = 0;
        if (device_lost_.load(std::memory_order_relaxed) || !StageLocked(chunk, 16, &ring_offset)) {
            ticket.ok = false;
            return ticket;
        }
        std::memcpy(staging_mapped_ + ring_offset, static_cast<const u8*>(data) + done, static_cast<usize>(chunk));
        usize slot = 0;
        ID3D12GraphicsCommandList* list = BeginInternalLocked(&slot);
        if (list == nullptr) {
            staging_ring_->Retire(last_signaled_);
            ticket.ok = false;
            return ticket;
        }
        // The buffer is promoted from COMMON to COPY_DEST by the copy and decays back when the list ends: the queue orders it after
        // every earlier submission and before every later one.
        list->CopyBufferRegion(resource, offset + done, staging_.Get(), ring_offset, chunk);
        const u64 value = SubmitInternalLocked(slot);
        if (value == 0) {
            staging_ring_->Retire(last_signaled_);
            ticket.ok = false;
            return ticket;
        }
        staging_ring_->Retire(value);
        ticket.fence = value;
        done += chunk;
    }
    return ticket;
}

bool Dx12Device::ReadbackBuffer(BufferHandle src, u64 offset, void* dst, u64 size) {
    ID3D12Resource* resource = nullptr;
    BufferRes snapshot;
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        const BufferRes* b = buffers_.Get(src);
        ENGINE_ASSERT(b != nullptr, "ReadbackBuffer: handle is invalid or already destroyed");
        if (b == nullptr) {
            return false;
        }
        ENGINE_ASSERT(Has(b->desc.usage, BufferUsage::CopySrc) || b->desc.location == MemoryLocation::GpuToCpu,
                      "ReadbackBuffer: the buffer was not created with CopySrc usage");
        ENGINE_ASSERT(dst != nullptr, "ReadbackBuffer: destination is null");
        ENGINE_ASSERT(size <= b->desc.size && offset <= b->desc.size - size, "ReadbackBuffer: range outside the buffer");
        resource = b->resource.Get();
        snapshot.desc = b->desc;
        snapshot.mapped = b->mapped;
    }
    if (size == 0) {
        return true;
    }
    if (device_lost_.load(std::memory_order_relaxed)) {
        return false;
    }
    if (snapshot.desc.location != MemoryLocation::GpuOnly) {
        u64 last = 0;
        {
            std::lock_guard<std::mutex> lock(submit_mutex_);
            last = last_signaled_;
        }
        if (!WaitForValue(last, ~0ull)) {
            return false;
        }
        std::memcpy(dst, snapshot.mapped + offset, static_cast<usize>(size));
        return true;
    }
    ComRef<ID3D12Resource> temp;
    u64 temp_bytes = 0;
    if (!CreateCommitted(D3D12_HEAP_TYPE_READBACK, BufferResourceDesc(size, D3D12_RESOURCE_FLAG_NONE), D3D12_RESOURCE_STATE_COPY_DEST, "engine readback",
                         &temp, &temp_bytes)) {
        return false;
    }
    u64 value = 0;
    {
        std::lock_guard<std::mutex> lock(submit_mutex_);
        usize slot = 0;
        ID3D12GraphicsCommandList* list = BeginInternalLocked(&slot);
        if (list != nullptr) {
            list->CopyBufferRegion(temp.Get(), 0, resource, offset, size); // the source is promoted to COPY_SOURCE for this list
            value = SubmitInternalLocked(slot);
        }
    }
    bool ok = value != 0 && WaitForValue(value, ~0ull);
    if (ok) {
        void* mapped = nullptr;
        D3D12_RANGE read{0, static_cast<SIZE_T>(size)};
        ok = Check(temp->Map(0, &read, &mapped), "Map (readback)");
        if (ok) {
            std::memcpy(dst, mapped, static_cast<usize>(size));
            D3D12_RANGE none{0, 0};
            temp->Unmap(0, &none);
        }
    }
    ReleaseResourceBytes(temp_bytes);
    return ok;
}

// ---- textures ----

TextureHandle Dx12Device::CreateTexture(const TextureDesc& desc) {
    if (const char* why = desc.Validate()) {
        RhiLog(LogVerbosity::Error, "CreateTexture: %s", why);
        return {};
    }
    if (!FormatSupport(desc.format, desc.usage)) {
        RhiLog(LogVerbosity::Error, "CreateTexture: the format %s cannot be used with these usages on this device", FormatInfo(desc.format).name);
        return {};
    }
    const bool is3d = desc.dimension == TextureDimension::Tex3D;
    const FormatProperties& fp = FormatInfo(desc.format);
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = is3d ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = desc.width;
    rd.Height = desc.height;
    rd.DepthOrArraySize = static_cast<UINT16>(is3d ? desc.depth : desc.array_layers);
    rd.MipLevels = static_cast<UINT16>(desc.mip_levels);
    rd.Format = ToDxgiResource(desc.format, desc.usage);
    rd.SampleDesc.Count = desc.samples;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (Has(desc.usage, TextureUsage::ColorAttachment)) rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (Has(desc.usage, TextureUsage::DepthAttachment)) {
        rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if (!Has(desc.usage, TextureUsage::Sampled)) rd.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    }
    if (Has(desc.usage, TextureUsage::Storage)) rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (desc.samples > 1) {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS ms{};
        ms.Format = ToDxgi(desc.format);
        ms.SampleCount = desc.samples;
        if (FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &ms, sizeof(ms))) || ms.NumQualityLevels == 0) {
            RhiLog(LogVerbosity::Error, "CreateTexture: %u samples of %s are not supported on this device", desc.samples, fp.name);
            return {};
        }
    }

    TextureRes res;
    res.desc = desc;
    res.desc.debug_name = nullptr;
    res.resource_format = rd.Format;
    if (!CreateCommitted(D3D12_HEAP_TYPE_DEFAULT, rd, D3D12_RESOURCE_STATE_COMMON, desc.debug_name, &res.resource, &res.bytes)) {
        return {};
    }
    ID3D12Resource* resource = res.resource.Get();
    const bool array = !is3d && desc.array_layers > 1;
    const bool ms = desc.samples > 1;

    std::lock_guard<std::mutex> lock(resource_mutex_);
    bool views_ok = true;
    if (Has(desc.usage, TextureUsage::Sampled)) {
        D3D12_SHADER_RESOURCE_VIEW_DESC v{};
        v.Format = ToDxgiShaderView(desc.format);
        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (is3d) {
            v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            v.Texture3D.MipLevels = desc.mip_levels;
        } else if (ms) {
            v.ViewDimension = array ? D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D12_SRV_DIMENSION_TEXTURE2DMS;
            v.Texture2DMSArray.ArraySize = desc.array_layers;
        } else if (array) {
            v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            v.Texture2DArray.MipLevels = desc.mip_levels;
            v.Texture2DArray.ArraySize = desc.array_layers;
        } else {
            v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            v.Texture2D.MipLevels = desc.mip_levels;
        }
        views_ok = views_ok && view_heap_.Allocate(&res.srv);
        if (views_ok) device_->CreateShaderResourceView(resource, &v, res.srv);
    }
    if (Has(desc.usage, TextureUsage::ColorAttachment)) {
        D3D12_RENDER_TARGET_VIEW_DESC v{};
        v.Format = ToDxgi(desc.format);
        if (ms) {
            v.ViewDimension = array ? D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY : D3D12_RTV_DIMENSION_TEXTURE2DMS;
            v.Texture2DMSArray.ArraySize = 1;
        } else if (array) {
            v.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            v.Texture2DArray.ArraySize = 1;
        } else if (is3d) {
            v.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
            v.Texture3D.WSize = 1;
        } else {
            v.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        }
        views_ok = views_ok && rtv_heap_.Allocate(&res.rtv);
        if (views_ok) device_->CreateRenderTargetView(resource, &v, res.rtv);
    }
    if (Has(desc.usage, TextureUsage::DepthAttachment)) {
        D3D12_DEPTH_STENCIL_VIEW_DESC v{};
        v.Format = ToDxgiDepthView(desc.format);
        if (ms) {
            v.ViewDimension = array ? D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D12_DSV_DIMENSION_TEXTURE2DMS;
            v.Texture2DMSArray.ArraySize = 1;
        } else if (array) {
            v.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            v.Texture2DArray.ArraySize = 1;
        } else {
            v.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        }
        views_ok = views_ok && dsv_heap_.Allocate(&res.dsv);
        if (views_ok) device_->CreateDepthStencilView(resource, &v, res.dsv);
    }
    if (Has(desc.usage, TextureUsage::Storage)) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC v{};
        v.Format = ToDxgi(desc.format);
        if (is3d) {
            v.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            v.Texture3D.WSize = desc.depth;
        } else if (array) {
            v.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            v.Texture2DArray.ArraySize = desc.array_layers;
        } else {
            v.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        }
        views_ok = views_ok && view_heap_.Allocate(&res.uav);
        if (views_ok) device_->CreateUnorderedAccessView(resource, nullptr, &v, res.uav);
    }
    if (!views_ok) {
        ReleaseTextureViews(res);
        ReleaseResourceBytes(res.bytes);
        return {};
    }
    const u64 bytes = res.bytes;
    const TextureHandle handle = textures_.Allocate(static_cast<TextureRes&&>(res));
    if (!handle.IsValid()) {
        RhiLog(LogVerbosity::Error, "CreateTexture: out of texture handles");
        ReleaseTextureViews(res);
        ReleaseResourceBytes(bytes);
    }
    return handle;
}

void Dx12Device::DestroyTexture(TextureHandle texture) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    TextureRes* t = textures_.Get(texture);
    ENGINE_ASSERT(t != nullptr, "DestroyTexture: handle is invalid or already destroyed");
    if (t == nullptr) {
        return;
    }
    ENGINE_ASSERT(!t->external, "DestroyTexture: a swapchain image belongs to its swapchain");
    textures_.Retire(texture, frame_open_.load(std::memory_order_relaxed) ? frame_number_ : frame_number_ + 1);
}

bool Dx12Device::LookupTexture(TextureHandle texture, TextureInfo* out) const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    const TextureRes* t = textures_.Get(texture);
    if (t == nullptr) {
        return false;
    }
    out->resource = t->resource.Get();
    out->desc = t->desc;
    out->resource_format = t->resource_format;
    out->rtv = t->rtv;
    out->dsv = t->dsv;
    out->srv = t->srv;
    out->uav = t->uav;
    return true;
}

D3D12_RESOURCE_STATES Dx12Device::TextureState(TextureHandle texture) const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    const TextureRes* t = textures_.Get(texture);
    return t != nullptr ? t->state : D3D12_RESOURCE_STATE_COMMON;
}

void Dx12Device::SetTextureState(TextureHandle texture, D3D12_RESOURCE_STATES state) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    if (TextureRes* t = textures_.Get(texture)) {
        t->state = state;
    }
}

UploadTicket Dx12Device::UploadTexture(TextureHandle dst, u32 mip, u32 layer, const void* data, u32 row_pitch) {
    ID3D12Resource* resource = nullptr;
    TextureDesc desc;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        const TextureRes* t = textures_.Get(dst);
        ENGINE_ASSERT(t != nullptr, "UploadTexture: handle is invalid or already destroyed");
        if (t == nullptr) {
            return UploadTicket{0, false};
        }
        ENGINE_ASSERT(Has(t->desc.usage, TextureUsage::CopyDst) && Has(t->desc.usage, TextureUsage::Sampled),
                      "UploadTexture: the texture needs Sampled and CopyDst usage");
        ENGINE_ASSERT(mip < t->desc.mip_levels && layer < t->desc.array_layers, "UploadTexture: mip or layer out of range");
        ENGINE_ASSERT(data != nullptr, "UploadTexture: data is null");
        ENGINE_ASSERT(t->upload_state == ResourceState::Undefined || t->upload_state == ResourceState::ShaderRead,
                      "UploadTexture: the texture must be in state Undefined or ShaderRead");
        ENGINE_ASSERT(!FormatInfo(t->desc.format).has_stencil, "UploadTexture: depth-stencil formats with a stencil aspect cannot be uploaded");
        resource = t->resource.Get();
        desc = t->desc;
        state = t->state;
    }
    const FormatProperties& fp = FormatInfo(desc.format);
    const u32 width = MipExtent(desc.width, mip);
    const u32 height = MipExtent(desc.height, mip);
    const u32 depth = desc.dimension == TextureDimension::Tex3D ? MipExtent(desc.depth, mip) : 1;
    const u64 row_bytes = FormatRowBytes(desc.format, width);
    const u64 src_pitch = row_pitch != 0 ? row_pitch : row_bytes;
    ENGINE_ASSERT(src_pitch >= row_bytes && src_pitch % fp.bytes_per_block == 0,
                  "UploadTexture: row_pitch is smaller than a row or not a multiple of the texel size");
    const u32 block_rows = (height + fp.block_height - 1) / fp.block_height;
    const u32 subresource = mip + layer * desc.mip_levels;
    // The subresource's footprint as D3D12 lays it out: the copyable format (R32_TYPELESS for a typeless depth texture), the physical
    // size (a block-compressed mip smaller than a block is one block) and a 256-byte row pitch.
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    const D3D12_RESOURCE_DESC rdesc = resource->GetDesc();
    device_->GetCopyableFootprints(&rdesc, subresource, 1, 0, &layout, nullptr, nullptr, nullptr);
    const u64 pitch = layout.Footprint.RowPitch;
    // A depth texture is copied whole (D3D12 allows no partial copy into a depth-stencil resource); a color level is split into bands of
    // rows that fit a staging chunk, one slice at a time.
    const bool whole = fp.is_depth;
    u32 rows_per_band = static_cast<u32>(kUploadChunkBytes / pitch);
    if (whole || rows_per_band > block_rows) rows_per_band = block_rows;
    if (rows_per_band == 0 || (whole && pitch * block_rows * depth > kUploadChunkBytes)) {
        RhiLog(LogVerbosity::Error, "UploadTexture: a level of %ux%u %s does not fit the staging chunk", width, height, fp.name);
        return UploadTicket{0, false};
    }
    const u32 slices_per_copy = whole ? depth : 1;
    D3D12_RESOURCE_STATES current = state;
    UploadTicket ticket;
    for (u32 z = 0; z < depth; z += slices_per_copy) {
        for (u32 row = 0; row < block_rows; row += rows_per_band) {
            const u32 rows = block_rows - row < rows_per_band ? block_rows - row : rows_per_band;
            const bool first = z == 0 && row == 0;
            const bool last = z + slices_per_copy >= depth && row + rows >= block_rows;
            std::lock_guard<std::mutex> lock(submit_mutex_);
            const u64 band_bytes = pitch * rows * slices_per_copy;
            u64 ring_offset = 0;
            if (device_lost_.load(std::memory_order_relaxed) || !StageLocked(band_bytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, &ring_offset)) {
                ticket.ok = false;
                break;
            }
            for (u32 s = 0; s < slices_per_copy; ++s) {
                for (u32 r = 0; r < rows; ++r) {
                    const u64 src_offset = (static_cast<u64>(z + s) * block_rows + row + r) * src_pitch;
                    std::memcpy(staging_mapped_ + ring_offset + (static_cast<u64>(s) * rows + r) * pitch, static_cast<const u8*>(data) + src_offset,
                                static_cast<usize>(row_bytes));
                }
            }
            usize slot = 0;
            ID3D12GraphicsCommandList* list = BeginInternalLocked(&slot);
            if (list == nullptr) {
                staging_ring_->Retire(last_signaled_);
                ticket.ok = false;
                break;
            }
            if (first && current != D3D12_RESOURCE_STATE_COPY_DEST) {
                const D3D12_RESOURCE_BARRIER b = Transition(resource, current, D3D12_RESOURCE_STATE_COPY_DEST);
                list->ResourceBarrier(1, &b);
                current = D3D12_RESOURCE_STATE_COPY_DEST;
            }
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = staging_.Get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Offset = ring_offset;
            src.PlacedFootprint.Footprint = layout.Footprint;
            if (!whole) {
                const u32 band_height = rows * fp.block_height;
                const u32 left = layout.Footprint.Height - row * fp.block_height;
                src.PlacedFootprint.Footprint.Height = band_height < left ? band_height : left;
                src.PlacedFootprint.Footprint.Depth = 1;
            }
            D3D12_TEXTURE_COPY_LOCATION dst_loc{};
            dst_loc.pResource = resource;
            dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst_loc.SubresourceIndex = subresource;
            if (whole) {
                list->CopyTextureRegion(&dst_loc, 0, 0, 0, &src, nullptr);
            } else {
                list->CopyTextureRegion(&dst_loc, 0, row * fp.block_height, z, &src, nullptr);
            }
            if (last) {
                const D3D12_RESOURCE_BARRIER b = Transition(resource, D3D12_RESOURCE_STATE_COPY_DEST, kShaderRead);
                list->ResourceBarrier(1, &b);
                current = kShaderRead;
            }
            const u64 value = SubmitInternalLocked(slot);
            if (value == 0) {
                staging_ring_->Retire(last_signaled_);
                ticket.ok = false;
                break;
            }
            staging_ring_->Retire(value);
            ticket.fence = value;
        }
        if (!ticket.ok) break;
    }
    if (!ticket.ok && current != kShaderRead && current != state) {
        // A failure after the first band left the texture in COPY_DEST: put it in ShaderRead anyway (the level only partly written).
        std::lock_guard<std::mutex> lock(submit_mutex_);
        usize slot = 0;
        if (ID3D12GraphicsCommandList* list = BeginInternalLocked(&slot)) {
            const D3D12_RESOURCE_BARRIER b = Transition(resource, current, kShaderRead);
            list->ResourceBarrier(1, &b);
            if (SubmitInternalLocked(slot) != 0) current = kShaderRead;
        }
    }
    std::lock_guard<std::mutex> lock(resource_mutex_);
    if (TextureRes* t = textures_.Get(dst)) {
        t->state = current;
        t->upload_state = ResourceState::ShaderRead;
    }
    return ticket;
}

bool Dx12Device::ReadbackTexture(TextureHandle src, u32 mip, u32 layer, void* dst, u64 dst_size, ResourceState state) {
    ID3D12Resource* resource = nullptr;
    TextureDesc desc;
    D3D12_RESOURCE_STATES actual = D3D12_RESOURCE_STATE_COMMON;
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        const TextureRes* t = textures_.Get(src);
        ENGINE_ASSERT(t != nullptr, "ReadbackTexture: handle is invalid or already destroyed");
        if (t == nullptr) {
            return false;
        }
        ENGINE_ASSERT(Has(t->desc.usage, TextureUsage::CopySrc), "ReadbackTexture: the texture was not created with CopySrc usage");
        ENGINE_ASSERT(mip < t->desc.mip_levels && layer < t->desc.array_layers, "ReadbackTexture: mip or layer out of range");
        ENGINE_ASSERT(dst != nullptr, "ReadbackTexture: destination is null");
        ENGINE_ASSERT(state != ResourceState::Undefined && state != ResourceState::Count, "ReadbackTexture: state must be a texture state");
        resource = t->resource.Get();
        desc = t->desc;
        // D3D12 needs the actual state as the transition's before-state: the one the device recorded after everything submitted. The
        // caller's `state` is what the RHI contract asks for; on this backend the record is authoritative.
        actual = t->state;
    }
    const FormatProperties& fp = FormatInfo(desc.format);
    const u32 width = MipExtent(desc.width, mip);
    const u32 height = MipExtent(desc.height, mip);
    const u32 depth = desc.dimension == TextureDimension::Tex3D ? MipExtent(desc.depth, mip) : 1;
    const u64 row_bytes = FormatRowBytes(desc.format, width);
    const u32 block_rows = (height + fp.block_height - 1) / fp.block_height;
    ENGINE_ASSERT(dst_size >= row_bytes * block_rows * depth, "ReadbackTexture: destination is smaller than the level");
    if (device_lost_.load(std::memory_order_relaxed)) {
        return false;
    }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    const D3D12_RESOURCE_DESC rdesc = resource->GetDesc();
    const u32 subresource = mip + layer * desc.mip_levels;
    device_->GetCopyableFootprints(&rdesc, subresource, 1, 0, &layout, nullptr, nullptr, nullptr);
    const u64 pitch = layout.Footprint.RowPitch;
    const u64 total = pitch * block_rows * depth;
    ComRef<ID3D12Resource> temp;
    u64 temp_bytes = 0;
    if (!CreateCommitted(D3D12_HEAP_TYPE_READBACK, BufferResourceDesc(total, D3D12_RESOURCE_FLAG_NONE), D3D12_RESOURCE_STATE_COPY_DEST,
                         "engine texture readback", &temp, &temp_bytes)) {
        return false;
    }
    u64 value = 0;
    {
        std::lock_guard<std::mutex> lock(submit_mutex_);
        usize slot = 0;
        ID3D12GraphicsCommandList* list = BeginInternalLocked(&slot);
        if (list != nullptr) {
            if (actual != D3D12_RESOURCE_STATE_COPY_SOURCE) {
                const D3D12_RESOURCE_BARRIER b = Transition(resource, actual, D3D12_RESOURCE_STATE_COPY_SOURCE);
                list->ResourceBarrier(1, &b);
            }
            D3D12_TEXTURE_COPY_LOCATION src_loc{};
            src_loc.pResource = resource;
            src_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src_loc.SubresourceIndex = subresource;
            D3D12_TEXTURE_COPY_LOCATION dst_loc{};
            dst_loc.pResource = temp.Get();
            dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst_loc.PlacedFootprint.Offset = 0;
            dst_loc.PlacedFootprint.Footprint = layout.Footprint;
            list->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);
            if (actual != D3D12_RESOURCE_STATE_COPY_SOURCE) {
                const D3D12_RESOURCE_BARRIER b = Transition(resource, D3D12_RESOURCE_STATE_COPY_SOURCE, actual);
                list->ResourceBarrier(1, &b);
            }
            value = SubmitInternalLocked(slot);
        }
    }
    bool ok = value != 0 && WaitForValue(value, ~0ull);
    if (ok) {
        u8* mapped = nullptr;
        D3D12_RANGE read{0, static_cast<SIZE_T>(total)};
        ok = Check(temp->Map(0, &read, reinterpret_cast<void**>(&mapped)), "Map (texture readback)");
        if (ok) {
            for (u32 z = 0; z < depth; ++z) {
                for (u32 r = 0; r < block_rows; ++r) {
                    std::memcpy(static_cast<u8*>(dst) + (static_cast<u64>(z) * block_rows + r) * row_bytes, mapped + (static_cast<u64>(z) * block_rows + r) * pitch,
                                static_cast<usize>(row_bytes));
                }
            }
            D3D12_RANGE none{0, 0};
            temp->Unmap(0, &none);
        }
    }
    ReleaseResourceBytes(temp_bytes);
    if (ToD3D12State(state) != actual) {
        RhiLog(LogVerbosity::Debug, "ReadbackTexture: the caller said %u, the device recorded D3D12 state 0x%x", static_cast<unsigned>(state),
               static_cast<unsigned>(actual));
    }
    return ok;
}

} // namespace engine_placeholder::rhi::dx12
