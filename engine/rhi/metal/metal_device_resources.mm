// MetalDevice: buffers, textures, the staging ring, and uploads and readbacks (Phase 21 Day 301).
//
// Storage modes follow the memory location: GpuOnly resources and every texture are Private; readback memory (GpuToCpu)
// and the staging ring are Shared; CPU-written memory (CpuToGpu) is Shared on a GPU with unified memory and Managed
// otherwise (macOS only; or when a test forces it, ForceManagedStorageForTest). A Managed buffer needs
// `didModifyRange:` after CPU writes (FlushMappedBuffer, UnmapBuffer and the CpuToGpu upload do it); the GPU never
// writes one. Copies between buffers go through the 4-byte-aligned window around the range (Metal's rule on macOS);
// copies between buffers and textures through a blit encoder with the rows packed as the caller gives them: Metal needs
// no row pitch beyond the texel size. Metal tracks hazards itself, so a texture's ResourceState is the RHI's
// bookkeeping only.

#include "cstr/mem_ops.h"
#include "sync/atomic.h"
#include "sync/mutex.h"

#include "build_defs.h"
#include "containers/vector.h"
#include "log/assert.h"
#include "rhi/metal/metal_device.h"
#include "rhi/metal/metal_format.h"
#include "rhi/rhi_log.h"

#import <Foundation/Foundation.h>

namespace vse::rhi::metal
{

namespace
{

constexpr u64 kStagingBytes = 64ull << 20;      // the upload ring
constexpr u64 kUploadChunkBytes = 16ull << 20;  // the largest part one upload submission stages
constexpr u64 kStagingWaitNs = 5ull * 1000 * 1000 * 1000;
constexpr u64 kStagingAlignment = 16;  // a multiple of every texel and block size

Atomic<bool> g_force_managed{false};
Atomic<u64> g_leaked_user_objects{0};

u64 AlignUp(u64 value, u64 alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

// Whether a storage mode is Managed (a mode macOS alone has).
bool StorageIsManaged([[maybe_unused]] MTLStorageMode mode)
{
#if ENGINE_PLATFORM_MACOS
    return mode == MTLStorageModeManaged;
#else
    return false;
#endif
}

NSString* LabelOf(const char* name)
{
    return name != nullptr && name[0] != '\0' ? [NSString stringWithUTF8String:name] : nil;
}

MTLTextureUsage UsageOf(TextureUsage usage)
{
    MTLTextureUsage u = MTLTextureUsageUnknown;
    if (Has(usage, TextureUsage::Sampled))
    {
        u |= MTLTextureUsageShaderRead;
    }
    if (Has(usage, TextureUsage::Storage))
    {
        u |= MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    }
    if (Has(usage, TextureUsage::ColorAttachment) || Has(usage, TextureUsage::DepthAttachment))
    {
        u |= MTLTextureUsageRenderTarget;
    }
    return u;
}

}  // namespace

void ForceManagedStorageForTest(bool on)
{
    g_force_managed.Store(on, MemoryOrder::Release);
}

u64 MetalDevice::LeakedUserObjects()
{
    return g_leaked_user_objects.Load(MemoryOrder::Relaxed);
}

void MetalDevice::ResetLeakedUserObjects()
{
    g_leaked_user_objects.Store(0, MemoryOrder::Relaxed);
}

void MetalDevice::NoteLeakedUserObjects(u64 count)
{
    g_leaked_user_objects.FetchAdd(count, MemoryOrder::Relaxed);
}

// ---- setup ----

bool MetalDevice::InitializeResources()
{
    const u32 seed = GenerationSeed(epoch_);
    buffers_.SeedGeneration(seed);
    textures_.SeedGeneration(seed);
    samplers_.SeedGeneration(seed);
    shaders_.SeedGeneration(seed);
    pipelines_.SeedGeneration(seed);
    bind_group_layouts_.SeedGeneration(seed);
    bind_groups_.SeedGeneration(seed);
#if ENGINE_PLATFORM_MACOS
    managed_ = !device_.hasUnifiedMemory || g_force_managed.Load(MemoryOrder::Acquire);
#else
    managed_ = false;  // iOS: no Managed mode
#endif
    // The staging ring is Shared on every Mac: the CPU writes it once and the GPU reads it once, so a Managed copy
    // would only add a transfer.
    staging_ = NewBuffer(kStagingBytes, MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined,
                         "engine staging ring", &staging_bytes_);
    if (staging_ == nil)
    {
        return false;
    }
    staging_mapped_ = static_cast<u8*>(staging_.contents);
    staging_ring_ = MakeUnique<RingAllocator>(kStagingBytes);
    return staging_ring_.Get() != nullptr;
}

void MetalDevice::ShutdownResources()
{
    // Every user resource is collected by now (the destructor's WaitIdle and collection); what is left was leaked by
    // the application and is released here with the device.
    buffers_.ForEachLive(
        [&](BufferHandle, BufferRes& b)
        {
            ReleaseBytes(b.bytes);
            b.buffer = nil;
        });
    textures_.ForEachLive(
        [&](TextureHandle, TextureRes& t)
        {
            if (!t.external)
            {
                ReleaseBytes(t.bytes);
            }
            t.texture = nil;
        });
    samplers_.ForEachLive([](SamplerHandle, SamplerRes& s) { s.state = nil; });
    pipelines_.ForEachLive([](PipelineHandle, PipelineRes& p) { p.info = MetalPipelineInfo{}; });
    shaders_.ForEachLive(
        [](ShaderHandle, ShaderRes& s)
        {
            s.function = nil;
            s.library = nil;
        });
    depth_states_.Clear();
    staging_ring_.Reset();
    if (staging_ != nil)
    {
        ReleaseBytes(staging_bytes_);
        staging_ = nil;
        staging_mapped_ = nullptr;
    }
}

void MetalDevice::CollectRetiredLocked(bool all)
{
    // What Metal autoreleases here is let go here: the caller (a job thread, a test) may have no pool that drains.
    @autoreleasepool
    {
        const u64 completed = all ? ~0ull : completed_frames_;
        pipelines_.CollectRetired(completed, [](PipelineRes& p) { p.info = MetalPipelineInfo{}; });
        shaders_.CollectRetired(completed,
                                [](ShaderRes& s)
                                {
                                    s.function = nil;
                                    s.library = nil;
                                });
        bind_groups_.CollectRetired(completed, [](BindGroupRes&) {});
        bind_group_layouts_.CollectRetired(completed, [](BindGroupLayoutRes&) {});
        samplers_.CollectRetired(completed, [](SamplerRes& s) { s.state = nil; });
        buffers_.CollectRetired(completed,
                                [&](BufferRes& b)
                                {
                                    ReleaseBytes(b.bytes);
                                    b.buffer = nil;
                                });
        textures_.CollectRetired(completed,
                                 [&](TextureRes& t)
                                 {
                                     if (!t.external)
                                     {
                                         ReleaseBytes(t.bytes);
                                     }
                                     t.texture = nil;
                                 });
    }
}

bool MetalDevice::CountAllocation(const char* name)
{
    // Each Metal buffer or texture is one device allocation: the Nth and every later one fail when a test injects it.
    const u32 allocation = allocation_count_.FetchAdd(1, MemoryOrder::Relaxed) + 1;
    if (fail_allocations_from_ != 0 && allocation >= fail_allocations_from_)
    {
        RhiLog(LogVerbosity::Error, "Metal: no memory for the resource '%s' (an injected allocation failure)",
               name != nullptr ? name : "(unnamed)");
        return false;
    }
    return true;
}

id<MTLBuffer> MetalDevice::NewBuffer(u64 size, MTLResourceOptions options, const char* name, u64* bytes)
{
    if (!CountAllocation(name))
    {
        return nil;
    }
    id<MTLBuffer> buffer = [device_ newBufferWithLength:static_cast<NSUInteger>(size) options:options];
    if (buffer == nil)
    {
        RhiLog(LogVerbosity::Error, "Metal: cannot create the buffer '%s' (%llu bytes)",
               name != nullptr ? name : "(unnamed)", static_cast<unsigned long long>(size));
        return nil;
    }
    buffer.label = LabelOf(name);
    *bytes = buffer.allocatedSize;
    device_memory_bytes_.FetchAdd(*bytes, MemoryOrder::Relaxed);
    raw_resources_.FetchAdd(1, MemoryOrder::Relaxed);
    return buffer;
}

void MetalDevice::ReleaseBytes(u64 bytes)
{
    device_memory_bytes_.FetchSub(bytes, MemoryOrder::Relaxed);
    raw_resources_.FetchSub(1, MemoryOrder::Relaxed);
}

MTLStorageMode MetalDevice::StorageModeFor(MemoryLocation location, BufferUsage usage) const
{
    // Readback memory is Shared on every Mac: the GPU's writes are then visible to the CPU once the work is done,
    // without a synchronizeResource: per read, and a persistent mapping stays valid. Only CPU-written memory is Managed
    // on a GPU without unified memory (macOS only: iOS has no Managed mode).
    if (location == MemoryLocation::GpuOnly)
    {
        return MTLStorageModePrivate;
    }
#if ENGINE_PLATFORM_MACOS
    // A buffer the GPU may write stays Shared (the end-of-phase review, Day 312): a Managed one's GPU writes reach the
    // CPU's copy only through a synchronizeResource: blit, and its readback and mapping read that copy.
    const bool gpu_writes = (usage & (BufferUsage::CopyDst | BufferUsage::Storage)) != BufferUsage::None;
    if (location == MemoryLocation::CpuToGpu && managed_ && !gpu_writes)
    {
        return MTLStorageModeManaged;
    }
#endif
    return MTLStorageModeShared;
}

void MetalDevice::NoteModified([[maybe_unused]] id<MTLBuffer> buffer, [[maybe_unused]] u64 offset,
                               [[maybe_unused]] u64 size)
{
#if ENGINE_PLATFORM_MACOS
    [buffer didModifyRange:NSMakeRange(static_cast<NSUInteger>(offset), static_cast<NSUInteger>(size))];
#else
    ENGINE_ASSERT(false, "NoteModified: a Managed buffer exists only on macOS");
#endif
}

// ---- internal submissions ----

id<MTLCommandBuffer> MetalDevice::BeginInternalLocked(NSString* label)
{
    if (DeviceLost())
    {
        RhiLog(LogVerbosity::Error, "Metal: %s skipped: the device is lost", label.UTF8String);
        return nil;
    }
    id<MTLCommandBuffer> buffer = [queue_ commandBuffer];
    if (buffer == nil)
    {
        NoteLost("an internal copy", "the queue gave no command buffer");
        return nil;
    }
    buffer.label = label;
    return buffer;
}

u64 MetalDevice::CommitInternalLocked(id<MTLCommandBuffer> buffer)
{
    WatchCommandBuffer(buffer);
    const u64 value = SignalAfterLocked(buffer);
    [buffer commit];
    return value;
}

bool MetalDevice::StageLocked(u64 size, u64 alignment, u64* offset)
{
    staging_ring_->Reclaim(CompletedFence());
    while (!staging_ring_->Allocate(size, alignment, offset))
    {
        const u64 pending = staging_ring_->OldestPendingFence();
        if (pending == 0 || !WaitForValue(pending, kStagingWaitNs))
        {
            RhiLog(LogVerbosity::Error, "Metal: the staging ring is exhausted and the GPU is not freeing it");
            return false;
        }
        staging_ring_->Reclaim(CompletedFence());
    }
    return true;
}

bool MetalDevice::BlitToCpu(u64 bytes, NSString* label, void (^encode)(id<MTLBlitCommandEncoder>, id<MTLBuffer>),
                            u8* dst)
{
    // What Metal autoreleases here is let go here: the caller (a job thread, a test) may have no pool that drains.
    @autoreleasepool
    {
        u64 temp_bytes = 0;
        u64 value = 0;
        id<MTLBuffer> temp = nil;
        @autoreleasepool
        {
            temp = NewBuffer(bytes, MTLResourceStorageModeShared, "engine readback", &temp_bytes);
            if (temp == nil)
            {
                return false;
            }
            LockGuard<Mutex> lock(submit_mutex_);
            id<MTLCommandBuffer> commands = BeginInternalLocked(label);
            if (commands != nil)
            {
                id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
                encode(blit, temp);
                [blit endEncoding];
                value = CommitInternalLocked(commands);
            }
        }
        const bool ok = value != 0 && WaitForValue(value, ~0ull);
        if (ok)
        {
            MemCopy(dst, temp.contents, static_cast<usize>(bytes));
        }
        temp = nil;
        ReleaseBytes(temp_bytes);
        return ok;
    }
}

bool MetalDevice::ReadGpuBuffer(id<MTLBuffer> source, u64 offset, u64 size, u8* dst)
{
    // Metal copies between buffers in multiples of 4 bytes at offsets that are multiples of 4 (macOS): the aligned
    // window around the range is copied and the range cut from it. The buffer's length is a multiple of 4
    // (CreateBuffer).
    const u64 start = offset & ~3ull;
    const u64 end = AlignUp(offset + size, 4);
    Vector<u8> window;
    u8* into = dst;
    if (start != offset || end != offset + size)
    {
        window.Resize(static_cast<usize>(end - start));
        into = window.Data();
    }
    const bool ok = BlitToCpu(
        end - start, @"engine buffer readback",
        ^(id<MTLBlitCommandEncoder> blit, id<MTLBuffer> temp) {
          [blit copyFromBuffer:source
                   sourceOffset:static_cast<NSUInteger>(start)
                       toBuffer:temp
              destinationOffset:0
                           size:static_cast<NSUInteger>(end - start)];
        },
        into);
    if (ok && into != dst)
    {
        MemCopy(dst, window.Data() + (offset - start), static_cast<usize>(size));
    }
    return ok;
}

UploadTicket MetalDevice::WriteGpuBuffer(id<MTLBuffer> target, u64 offset, const u8* data, u64 size)
{
    // Aligned as ReadGpuBuffer: an unaligned range is widened to the 4-byte window around it, whose edge bytes are read
    // back first (after everything submitted) so the copy leaves the bytes outside the range as they were.
    const u64 start = offset & ~3ull;
    const u64 end = AlignUp(offset + size, 4);
    Vector<u8> window;
    if (start != offset || end != offset + size)
    {
        window.Resize(static_cast<usize>(end - start));
        if (!ReadGpuBuffer(target, start, end - start, window.Data()))
        {
            return UploadTicket{0, false};
        }
        MemCopy(window.Data() + (offset - start), data, static_cast<usize>(size));
        data = window.Data();
    }
    UploadTicket ticket;
    u64 done = 0;
    const u64 total = end - start;
    while (done < total)
    {
        const u64 chunk = total - done < kUploadChunkBytes ? total - done : kUploadChunkBytes;  // multiples of 4
        @autoreleasepool
        {
            LockGuard<Mutex> lock(submit_mutex_);
            u64 ring_offset = 0;
            id<MTLCommandBuffer> commands = nil;
            if (!StageLocked(chunk, kStagingAlignment, &ring_offset))
            {
                ticket.ok = false;
                return ticket;
            }
            MemCopy(staging_mapped_ + ring_offset, data + done, static_cast<usize>(chunk));
            commands = BeginInternalLocked(@"engine buffer upload");
            if (commands == nil)
            {
                staging_ring_->Retire(last_signaled_);
                ticket.ok = false;
                return ticket;
            }
            id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
            [blit copyFromBuffer:staging_
                     sourceOffset:static_cast<NSUInteger>(ring_offset)
                         toBuffer:target
                destinationOffset:static_cast<NSUInteger>(start + done)
                             size:static_cast<NSUInteger>(chunk)];
            [blit endEncoding];
            const u64 value = CommitInternalLocked(commands);
            staging_ring_->Retire(value);
            ticket.fence = value;
        }
        done += chunk;
    }
    return ticket;
}

// ---- buffers ----

BufferHandle MetalDevice::CreateBuffer(const BufferDesc& desc)
{
    if (const char* why = desc.Validate())
    {
        RhiLog(LogVerbosity::Error, "CreateBuffer: %s", why);
        return {};
    }
    BufferRes res;
    res.desc = desc;
    res.desc.debug_name = nullptr;
    const MTLStorageMode mode = StorageModeFor(desc.location, desc.usage);
    MTLResourceOptions options = static_cast<MTLResourceOptions>(mode) << MTLResourceStorageModeShift;
    if (desc.location == MemoryLocation::CpuToGpu)
    {
        options |= MTLResourceCPUCacheModeWriteCombined;  // written by the CPU, never read back
    }
    @autoreleasepool
    {
        // A multiple of 4 bytes long, so the aligned window of any range (ReadGpuBuffer) lies inside it.
        res.buffer = NewBuffer(AlignUp(desc.size, 4), options, desc.debug_name, &res.bytes);
    }
    if (res.buffer == nil)
    {
        return {};
    }
    res.managed = StorageIsManaged(mode);
    res.mapped = mode == MTLStorageModePrivate ? nullptr : static_cast<u8*>(res.buffer.contents);
    LockGuard<Mutex> lock(resource_mutex_);
    const u64 bytes = res.bytes;
    const BufferHandle handle = buffers_.Allocate(static_cast<BufferRes&&>(res));
    if (!handle.IsValid())
    {
        RhiLog(LogVerbosity::Error, "CreateBuffer: out of buffer handles");
        ReleaseBytes(bytes);
    }
    return handle;
}

void MetalDevice::DestroyBuffer(BufferHandle buffer)
{
    LockGuard<Mutex> lock(resource_mutex_);
    BufferRes* b = buffers_.Get(buffer);
    ENGINE_ASSERT(b != nullptr, "DestroyBuffer: handle is invalid or already destroyed");
    if (b == nullptr)
    {
        return;
    }
    ENGINE_ASSERT(!b->user_mapped, "DestroyBuffer: the buffer is still mapped");
    // Work in the open frame may still use it; outside a frame, the next frame is the earliest that can have used it.
    buffers_.Retire(buffer, frame_open_.Load(MemoryOrder::Relaxed) ? frame_number_ : frame_number_ + 1);
}

id<MTLBuffer> MetalDevice::BufferObject(BufferHandle buffer) const
{
    LockGuard<Mutex> lock(resource_mutex_);
    const BufferRes* b = buffers_.Get(buffer);
    return b != nullptr ? b->buffer : nil;
}

void* MetalDevice::MapBuffer(BufferHandle buffer)
{
    LockGuard<Mutex> lock(resource_mutex_);
    BufferRes* b = buffers_.Get(buffer);
    ENGINE_ASSERT(b != nullptr, "MapBuffer: handle is invalid or already destroyed");
    if (b == nullptr)
    {
        return nullptr;
    }
    ENGINE_ASSERT(b->desc.location != MemoryLocation::GpuOnly, "MapBuffer: the buffer is not CPU-visible");
    ENGINE_ASSERT(!b->user_mapped, "MapBuffer: the buffer is already mapped");
    b->user_mapped = true;
    return b->mapped;  // persistent: a GpuToCpu buffer is Shared, so finished GPU writes are in it
}

void MetalDevice::UnmapBuffer(BufferHandle buffer)
{
    LockGuard<Mutex> lock(resource_mutex_);
    BufferRes* b = buffers_.Get(buffer);
    ENGINE_ASSERT(b != nullptr, "UnmapBuffer: handle is invalid or already destroyed");
    if (b == nullptr)
    {
        return;
    }
    ENGINE_ASSERT(b->user_mapped, "UnmapBuffer: the buffer is not mapped");
    b->user_mapped = false;
    if (b->managed)
    {
        // The writer may not have flushed: the whole buffer is sent (a Shared buffer is coherent and needs nothing).
        NoteModified(b->buffer, 0, b->desc.size);
    }
}

void MetalDevice::FlushMappedBuffer(BufferHandle buffer, u64 offset, u64 size)
{
    LockGuard<Mutex> lock(resource_mutex_);
    BufferRes* b = buffers_.Get(buffer);
    ENGINE_ASSERT(b != nullptr, "FlushMappedBuffer: handle is invalid or already destroyed");
    if (b == nullptr)
    {
        return;
    }
    ENGINE_ASSERT(size <= b->desc.size && offset <= b->desc.size - size, "FlushMappedBuffer: range outside the buffer");
    if (b->managed && size != 0)  // only CpuToGpu memory is ever Managed
    {
        NoteModified(b->buffer, offset, size);
    }
}

UploadTicket MetalDevice::UploadBuffer(BufferHandle dst, u64 offset, const void* data, u64 size)
{
    id<MTLBuffer> target = nil;
    BufferDesc desc;
    u8* mapped = nullptr;
    bool managed = false;
    {
        LockGuard<Mutex> lock(resource_mutex_);
        const BufferRes* b = buffers_.Get(dst);
        ENGINE_ASSERT(b != nullptr, "UploadBuffer: handle is invalid or already destroyed");
        if (b == nullptr)
        {
            return UploadTicket{0, false};
        }
        ENGINE_ASSERT(Has(b->desc.usage, BufferUsage::CopyDst),
                      "UploadBuffer: the buffer was not created with CopyDst usage");
        ENGINE_ASSERT(data != nullptr, "UploadBuffer: data is null");
        ENGINE_ASSERT(size <= b->desc.size && offset <= b->desc.size - size, "UploadBuffer: range outside the buffer");
        target = b->buffer;
        desc = b->desc;
        mapped = b->mapped;
        managed = b->managed;
    }
    if (size == 0)
    {
        return {};
    }
    if (desc.location == MemoryLocation::CpuToGpu)
    {
        // Written at once through the mapping (the RHI's rule for CpuToGpu memory); a Managed buffer sends the range.
        MemCopy(mapped + offset, data, static_cast<usize>(size));
        if (managed)
        {
            NoteModified(target, offset, size);
        }
        return {};
    }
    return WriteGpuBuffer(target, offset, static_cast<const u8*>(data), size);
}

bool MetalDevice::ReadbackBuffer(BufferHandle src, u64 offset, void* dst, u64 size)
{
    id<MTLBuffer> source = nil;
    BufferDesc desc;
    u8* mapped = nullptr;
    {
        LockGuard<Mutex> lock(resource_mutex_);
        const BufferRes* b = buffers_.Get(src);
        ENGINE_ASSERT(b != nullptr, "ReadbackBuffer: handle is invalid or already destroyed");
        if (b == nullptr)
        {
            return false;
        }
        ENGINE_ASSERT(Has(b->desc.usage, BufferUsage::CopySrc) || b->desc.location == MemoryLocation::GpuToCpu,
                      "ReadbackBuffer: the buffer was not created with CopySrc usage");
        ENGINE_ASSERT(dst != nullptr, "ReadbackBuffer: destination is null");
        ENGINE_ASSERT(size <= b->desc.size && offset <= b->desc.size - size,
                      "ReadbackBuffer: range outside the buffer");
        source = b->buffer;
        desc = b->desc;
        mapped = b->mapped;
    }
    if (size == 0)
    {
        return true;
    }
    if (DeviceLost())
    {
        RhiLog(LogVerbosity::Error, "ReadbackBuffer: the device is lost");
        return false;
    }
    if (desc.location != MemoryLocation::GpuOnly)
    {
        // CPU-visible memory holds the latest bytes once everything submitted is done: a GpuToCpu buffer is Shared, and
        // the CPU's own copy of a Managed CpuToGpu buffer is the one the GPU was sent.
        u64 last = 0;
        {
            LockGuard<Mutex> lock(submit_mutex_);
            last = last_signaled_;
        }
        if (!WaitForValue(last, ~0ull))
        {
            return false;
        }
        MemCopy(dst, mapped + offset, static_cast<usize>(size));
        return true;
    }
    return ReadGpuBuffer(source, offset, size, static_cast<u8*>(dst));
}

// ---- textures ----

TextureHandle MetalDevice::CreateTexture(const TextureDesc& desc)
{
    if (const char* why = desc.Validate())
    {
        RhiLog(LogVerbosity::Error, "CreateTexture: %s", why);
        return {};
    }
    if (!FormatSupport(desc.format, desc.usage))
    {
        RhiLog(LogVerbosity::Error, "CreateTexture: the format %s cannot be used with these usages on this device",
               FormatInfo(desc.format).name);
        return {};
    }
    if (desc.samples > 1 && ![device_ supportsTextureSampleCount:desc.samples])
    {
        RhiLog(LogVerbosity::Error, "CreateTexture: %u samples are not supported on this device", desc.samples);
        return {};
    }
    const bool is3d = desc.dimension == TextureDimension::Tex3D;
    const bool array = !is3d && desc.array_layers > 1;
    const bool ms = desc.samples > 1;
    TextureRes res;
    res.desc = desc;
    res.desc.debug_name = nullptr;
    @autoreleasepool
    {
        MTLTextureDescriptor* d = [[MTLTextureDescriptor alloc] init];
        if (is3d)
        {
            d.textureType = MTLTextureType3D;
        }
        else if (ms)
        {
            d.textureType = array ? MTLTextureType2DMultisampleArray : MTLTextureType2DMultisample;
        }
        else
        {
            d.textureType = array ? MTLTextureType2DArray : MTLTextureType2D;
        }
        d.pixelFormat = ToMtlPixelFormat(desc.format, device_);
        d.width = desc.width;
        d.height = desc.height;
        d.depth = is3d ? desc.depth : 1;
        d.mipmapLevelCount = desc.mip_levels;
        d.arrayLength = array ? desc.array_layers : 1;
        d.sampleCount = desc.samples;
        d.storageMode = MTLStorageModePrivate;
        d.usage = UsageOf(desc.usage);
        if (!CountAllocation(desc.debug_name))
        {
            return {};
        }
        res.texture = [device_ newTextureWithDescriptor:d];
        if (res.texture == nil)
        {
            RhiLog(LogVerbosity::Error, "CreateTexture: Metal could not create the %ux%u %s texture '%s'", desc.width,
                   desc.height, FormatInfo(desc.format).name, desc.debug_name != nullptr ? desc.debug_name : "");
            return {};
        }
        res.texture.label = LabelOf(desc.debug_name);
        res.bytes = res.texture.allocatedSize;
    }
    device_memory_bytes_.FetchAdd(res.bytes, MemoryOrder::Relaxed);
    raw_resources_.FetchAdd(1, MemoryOrder::Relaxed);
    LockGuard<Mutex> lock(resource_mutex_);
    const u64 bytes = res.bytes;
    const TextureHandle handle = textures_.Allocate(static_cast<TextureRes&&>(res));
    if (!handle.IsValid())
    {
        RhiLog(LogVerbosity::Error, "CreateTexture: out of texture handles");
        ReleaseBytes(bytes);
    }
    return handle;
}

void MetalDevice::DestroyTexture(TextureHandle texture)
{
    LockGuard<Mutex> lock(resource_mutex_);
    TextureRes* t = textures_.Get(texture);
    ENGINE_ASSERT(t != nullptr, "DestroyTexture: handle is invalid or already destroyed");
    if (t == nullptr)
    {
        return;
    }
    ENGINE_ASSERT(!t->external, "DestroyTexture: a swapchain image belongs to its swapchain");
    textures_.Retire(texture, frame_open_.Load(MemoryOrder::Relaxed) ? frame_number_ : frame_number_ + 1);
}

id<MTLTexture> MetalDevice::TextureObject(TextureHandle texture) const
{
    LockGuard<Mutex> lock(resource_mutex_);
    const TextureRes* t = textures_.Get(texture);
    return t != nullptr ? t->texture : nil;
}

UploadTicket MetalDevice::UploadTexture(TextureHandle dst, u32 mip, u32 layer, const void* data, u32 row_pitch)
{
    id<MTLTexture> texture = nil;
    TextureDesc desc;
    {
        LockGuard<Mutex> lock(resource_mutex_);
        const TextureRes* t = textures_.Get(dst);
        ENGINE_ASSERT(t != nullptr, "UploadTexture: handle is invalid or already destroyed");
        if (t == nullptr)
        {
            return UploadTicket{0, false};
        }
        ENGINE_ASSERT(Has(t->desc.usage, TextureUsage::CopyDst) && Has(t->desc.usage, TextureUsage::Sampled),
                      "UploadTexture: the texture needs Sampled and CopyDst usage");
        ENGINE_ASSERT(mip < t->desc.mip_levels && layer < t->desc.array_layers,
                      "UploadTexture: mip or layer out of range");
        ENGINE_ASSERT(data != nullptr, "UploadTexture: data is null");
        ENGINE_ASSERT(t->upload_state == ResourceState::Undefined || t->upload_state == ResourceState::ShaderRead,
                      "UploadTexture: the texture must be in state Undefined or ShaderRead");
        ENGINE_ASSERT(!FormatInfo(t->desc.format).has_stencil,
                      "UploadTexture: depth-stencil formats with a stencil aspect cannot be uploaded");
        ENGINE_ASSERT(t->desc.samples == 1, "UploadTexture: a multisampled texture cannot be uploaded");
        texture = t->texture;
        desc = t->desc;
    }
    const FormatProperties& fp = FormatInfo(desc.format);
    const bool is3d = desc.dimension == TextureDimension::Tex3D;
    const u32 width = MipExtent(desc.width, mip);
    const u32 height = MipExtent(desc.height, mip);
    const u32 depth = is3d ? MipExtent(desc.depth, mip) : 1;
    const u64 row_bytes = FormatRowBytes(desc.format, width);
    const u64 src_pitch = row_pitch != 0 ? row_pitch : row_bytes;
    ENGINE_ASSERT(src_pitch >= row_bytes && src_pitch % fp.bytes_per_block == 0,
                  "UploadTexture: row_pitch is smaller than a row or not a multiple of the texel size");
    const u32 block_rows = (height + fp.block_height - 1) / fp.block_height;
    // A level goes in bands of rows that fit a staging chunk, one slice at a time (Metal copies any region of a color
    // or depth-only texture, unlike D3D12's whole depth subresources). The staged rows are packed: Metal needs no pitch
    // beyond the row.
    u32 rows_per_band = static_cast<u32>(kUploadChunkBytes / row_bytes);
    if (rows_per_band > block_rows)
    {
        rows_per_band = block_rows;
    }
    if (rows_per_band == 0)
    {
        RhiLog(LogVerbosity::Error, "UploadTexture: a row of %u %s texels does not fit the staging chunk", width,
               fp.name);
        return UploadTicket{0, false};
    }
    if (DeviceLost())
    {
        RhiLog(LogVerbosity::Error, "UploadTexture: the device is lost");
        return UploadTicket{0, false};
    }
    const u32 slices_per_copy = 1;
    UploadTicket ticket;
    for (u32 z = 0; z < depth && ticket.ok; z += slices_per_copy)
    {
        for (u32 row = 0; row < block_rows; row += rows_per_band)
        {
            const u32 rows = block_rows - row < rows_per_band ? block_rows - row : rows_per_band;
            @autoreleasepool
            {
                LockGuard<Mutex> lock(submit_mutex_);
                const u64 band_bytes = row_bytes * rows * slices_per_copy;
                u64 ring_offset = 0;
                if (!StageLocked(band_bytes, kStagingAlignment, &ring_offset))
                {
                    ticket.ok = false;
                    break;
                }
                for (u32 s = 0; s < slices_per_copy; ++s)
                {
                    for (u32 r = 0; r < rows; ++r)
                    {
                        const u64 src_offset = (static_cast<u64>(z + s) * block_rows + row + r) * src_pitch;
                        MemCopy(staging_mapped_ + ring_offset + (static_cast<u64>(s) * rows + r) * row_bytes,
                                static_cast<const u8*>(data) + src_offset, static_cast<usize>(row_bytes));
                    }
                }
                id<MTLCommandBuffer> commands = BeginInternalLocked(@"engine texture upload");
                if (commands == nil)
                {
                    staging_ring_->Retire(last_signaled_);
                    ticket.ok = false;
                    break;
                }
                // The band in texels: the last band of a block-compressed level ends at the level's edge.
                const u32 top = row * fp.block_height;
                const u32 band_height = rows * fp.block_height < height - top ? rows * fp.block_height : height - top;
                id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
                [blit copyFromBuffer:staging_
                           sourceOffset:static_cast<NSUInteger>(ring_offset)
                      sourceBytesPerRow:static_cast<NSUInteger>(row_bytes)
                    sourceBytesPerImage:static_cast<NSUInteger>(row_bytes * rows)
                             sourceSize:MTLSizeMake(width, band_height, slices_per_copy)
                              toTexture:texture
                       destinationSlice:is3d ? 0 : layer
                       destinationLevel:mip
                      destinationOrigin:MTLOriginMake(0, top, z)];
                [blit endEncoding];
                const u64 value = CommitInternalLocked(commands);
                staging_ring_->Retire(value);
                ticket.fence = value;
            }
        }
    }
    LockGuard<Mutex> lock(resource_mutex_);
    if (TextureRes* t = textures_.Get(dst))
    {
        t->upload_state = ResourceState::ShaderRead;  // a failed upload leaves the level partly written, still readable
    }
    return ticket;
}

bool MetalDevice::ReadbackTexture(TextureHandle src, u32 mip, u32 layer, void* dst, u64 dst_size, ResourceState state)
{
    id<MTLTexture> texture = nil;
    TextureDesc desc;
    {
        LockGuard<Mutex> lock(resource_mutex_);
        const TextureRes* t = textures_.Get(src);
        ENGINE_ASSERT(t != nullptr, "ReadbackTexture: handle is invalid or already destroyed");
        if (t == nullptr)
        {
            return false;
        }
        ENGINE_ASSERT(Has(t->desc.usage, TextureUsage::CopySrc),
                      "ReadbackTexture: the texture was not created with CopySrc usage");
        ENGINE_ASSERT(mip < t->desc.mip_levels && layer < t->desc.array_layers,
                      "ReadbackTexture: mip or layer out of range");
        ENGINE_ASSERT(dst != nullptr, "ReadbackTexture: destination is null");
        // Metal tracks the texture's use itself: the state is checked as the contract asks and otherwise not needed.
        ENGINE_ASSERT(state != ResourceState::Undefined && state != ResourceState::Count,
                      "ReadbackTexture: state must be a texture state");
        ENGINE_ASSERT(!FormatInfo(t->desc.format).has_stencil,
                      "ReadbackTexture: depth-stencil formats with a stencil aspect cannot be read back");
        ENGINE_ASSERT(t->desc.samples == 1, "ReadbackTexture: a multisampled texture cannot be read back (resolve it)");
        texture = t->texture;
        desc = t->desc;
    }
    if (texture == nil)
    {
        // A swapchain image between frames: the drawable is given up at Present, so there is nothing to read.
        RhiLog(LogVerbosity::Error, "ReadbackTexture: the swapchain image has no drawable (read it before Present)");
        return false;
    }
    const FormatProperties& fp = FormatInfo(desc.format);
    const bool is3d = desc.dimension == TextureDimension::Tex3D;
    const u32 width = MipExtent(desc.width, mip);
    const u32 height = MipExtent(desc.height, mip);
    const u32 depth = is3d ? MipExtent(desc.depth, mip) : 1;
    const u64 row_bytes = FormatRowBytes(desc.format, width);
    const u32 block_rows = (height + fp.block_height - 1) / fp.block_height;
    const u64 total = row_bytes * block_rows * depth;
    ENGINE_ASSERT(dst_size >= total, "ReadbackTexture: destination is smaller than the level");
    if (DeviceLost())
    {
        RhiLog(LogVerbosity::Error, "ReadbackTexture: the device is lost");
        return false;
    }
    const u32 slice = is3d ? 0 : layer;
    return BlitToCpu(
        total, @"engine texture readback",
        ^(id<MTLBlitCommandEncoder> blit, id<MTLBuffer> temp) {
          [blit copyFromTexture:texture
                           sourceSlice:slice
                           sourceLevel:mip
                          sourceOrigin:MTLOriginMake(0, 0, 0)
                            sourceSize:MTLSizeMake(width, height, depth)
                              toBuffer:temp
                     destinationOffset:0
                destinationBytesPerRow:static_cast<NSUInteger>(row_bytes)
              destinationBytesPerImage:static_cast<NSUInteger>(row_bytes * block_rows)];
        },
        static_cast<u8*>(dst));
}

}  // namespace vse::rhi::metal
