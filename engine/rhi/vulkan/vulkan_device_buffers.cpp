// VulkanDevice: buffers, the timeline, the staging ring and the upload/readback paths, and deferred destruction.

#include <cstring>

#include "log/assert.h"
#include "rhi/rhi_log.h"
#include "rhi/vulkan/vk_result.h"
#include "rhi/vulkan/vulkan_device.h"

namespace engine_placeholder::rhi::vulkan {

namespace {

constexpr u64 kStagingRingBytes = 16ull << 20;
constexpr u64 kUploadChunkBytes = kStagingRingBytes / 4;
constexpr u64 kReadbackChunkBytes = 64ull << 20;
constexpr u64 kStagingWaitNs = 10ull * 1000 * 1000 * 1000;

VkBufferUsageFlags ToVkBufferUsage(BufferUsage usage) {
    VkBufferUsageFlags flags = 0;
    if (Has(usage, BufferUsage::Vertex)) flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (Has(usage, BufferUsage::Index)) flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (Has(usage, BufferUsage::Uniform)) flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (Has(usage, BufferUsage::Storage)) flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (Has(usage, BufferUsage::CopySrc)) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (Has(usage, BufferUsage::CopyDst)) flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (Has(usage, BufferUsage::Indirect)) flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    return flags;
}

} // namespace

// ---- raw buffers ----

bool VulkanDevice::CreateRawBuffer(u64 size, VkBufferUsageFlags usage, MemoryLocation location, const char* what, RawBuffer* out) {
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // one queue family does all the work
    VkBuffer buffer = nullptr;
    VkResult result = fns_.CreateBuffer(device_, &info, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        NoteDeviceLost(result);
        RhiLog(LogVerbosity::Error, "Vulkan: cannot create %s buffer of %llu bytes (%s)", what, static_cast<unsigned long long>(size),
               VkResultName(result));
        return false;
    }

    VkMemoryDedicatedRequirements dedicated_req{};
    dedicated_req.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
    VkMemoryRequirements2 req{};
    req.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    req.pNext = &dedicated_req;
    VkBufferMemoryRequirementsInfo2 req_info{};
    req_info.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
    req_info.buffer = buffer;
    fns_.GetBufferMemoryRequirements2(device_, &req_info, &req);

    MemoryRequest request;
    request.size = req.memoryRequirements.size;
    request.alignment = req.memoryRequirements.alignment;
    request.memory_type_bits = req.memoryRequirements.memoryTypeBits;
    request.location = location;
    request.linear = true;
    request.dedicated = dedicated_req.requiresDedicatedAllocation == VK_TRUE || dedicated_req.prefersDedicatedAllocation == VK_TRUE;
    request.dedicated_buffer = request.dedicated ? HandleToU64(buffer) : 0;
    MemoryResult memory_result = MemoryResult::Ok;
    MemoryAllocation allocation = memory_->Allocate(request, &memory_result);
    if (!allocation.Valid()) {
        fns_.DestroyBuffer(device_, buffer, nullptr);
        RhiLog(LogVerbosity::Error, "Vulkan: no memory for %s buffer of %llu bytes", what, static_cast<unsigned long long>(size));
        return false;
    }
    result = fns_.BindBufferMemory(device_, buffer, HandleFromU64<VkDeviceMemory>(allocation.memory), allocation.offset);
    if (result != VK_SUCCESS) {
        NoteDeviceLost(result);
        ENGINE_VK_CHECK(result);
        memory_->Free(allocation);
        fns_.DestroyBuffer(device_, buffer, nullptr);
        return false;
    }
    out->buffer = buffer;
    out->memory = allocation;
    out->size = size;
    raw_buffer_count_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void VulkanDevice::DestroyRawBuffer(RawBuffer* raw) {
    if (raw->buffer == nullptr) {
        return;
    }
    fns_.DestroyBuffer(device_, raw->buffer, nullptr);
    memory_->Free(raw->memory);
    raw_buffer_count_.fetch_sub(1, std::memory_order_relaxed);
    *raw = RawBuffer{};
}

void VulkanDevice::SetDebugName(u64 handle, VkObjectType type, const char* name) {
    if (name == nullptr || !instance_.ValidationEnabled() || fns_.SetDebugUtilsObjectNameEXT == nullptr) {
        return;
    }
    VkDebugUtilsObjectNameInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    fns_.SetDebugUtilsObjectNameEXT(device_, &info);
}

void VulkanDevice::NoteDeviceLost(VkResult result) {
    if (result == VK_ERROR_DEVICE_LOST && !device_lost_.exchange(true, std::memory_order_relaxed)) {
        RhiLog(LogVerbosity::Error, "Vulkan: the device was lost; further submissions are ignored");
        // A trace of what the program was doing when the loss was noticed, once (the first loss); the frame goes on to end as
        // DeviceLost. The crash report's `gpu` line names the adapter.
        ENGINE_ENSURE(false, "the Vulkan device was lost");
    }
}

void VulkanDevice::SyncRange(const MemoryAllocation& a, u64 rel_offset, u64 length, bool flush) {
    if (a.coherent || a.mapped == nullptr) {
        return;
    }
    // The allocator keeps ranges in non-coherent memory atom-aligned, so rounding stays inside the allocation.
    const u64 atom = properties_.limits.nonCoherentAtomSize;
    u64 begin = (a.offset + rel_offset) & ~(atom - 1);
    u64 end = (a.offset + rel_offset + length + atom - 1) & ~(atom - 1);
    if (end > a.offset + a.size) {
        end = a.offset + a.size;
    }
    VkMappedMemoryRange range{};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = HandleFromU64<VkDeviceMemory>(a.memory);
    range.offset = begin;
    range.size = end - begin;
    NoteDeviceLost(flush ? fns_.FlushMappedMemoryRanges(device_, 1, &range) : fns_.InvalidateMappedMemoryRanges(device_, 1, &range));
}

void VulkanDevice::FlushRange(const MemoryAllocation& a, u64 rel_offset, u64 length) { SyncRange(a, rel_offset, length, true); }
void VulkanDevice::InvalidateRange(const MemoryAllocation& a, u64 rel_offset, u64 length) { SyncRange(a, rel_offset, length, false); }

// ---- timeline, staging, internal submissions ----

void VulkanDevice::InitializeTimeline() {
    VkSemaphoreTypeCreateInfo type_info{};
    type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type_info.initialValue = 0;
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.pNext = &type_info;
    ENGINE_VK_CHECK(fns_.CreateSemaphore(device_, &info, nullptr, &timeline_));
}

bool VulkanDevice::InitializeUpload() {
    if (timeline_ == nullptr) {
        return false;
    }
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = static_cast<u32>(queues_.graphics_family);
    if (ENGINE_VK_CHECK(fns_.CreateCommandPool(device_, &pool_info, nullptr, &upload_pool_)) != VkResultClass::Success) {
        return false;
    }
    if (!CreateRawBuffer(kStagingRingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryLocation::CpuToGpu, "staging", &staging_) ||
        staging_.memory.mapped == nullptr) {
        return false;
    }
    SetDebugName(HandleToU64(staging_.buffer), VK_OBJECT_TYPE_BUFFER, "rhi staging ring");
    staging_ring_ = MakeUnique<RingAllocator>(kStagingRingBytes);
    return static_cast<bool>(staging_ring_);
}

void VulkanDevice::ShutdownUpload() {
    staging_ring_.Reset();
    DestroyRawBuffer(&staging_);
    if (upload_pool_ != nullptr) {
        fns_.DestroyCommandPool(device_, upload_pool_, nullptr); // frees its command buffers
        upload_pool_ = nullptr;
    }
    upload_buffers_.Clear();
    if (timeline_ != nullptr) {
        fns_.DestroySemaphore(device_, timeline_, nullptr);
        timeline_ = nullptr;
    }
}

u64 VulkanDevice::CompletedTimeline() const {
    if (timeline_ == nullptr) {
        return 0;
    }
    u64 value = 0;
    VkResult r = fns_.GetSemaphoreCounterValue(device_, timeline_, &value);
    if (r != VK_SUCCESS) {
        const_cast<VulkanDevice*>(this)->NoteDeviceLost(r);
        return 0;
    }
    return value;
}

bool VulkanDevice::WaitTimeline(u64 value, u64 timeout_ns) {
    if (value == 0) {
        return true;
    }
    VkSemaphoreWaitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    info.semaphoreCount = 1;
    info.pSemaphores = &timeline_;
    info.pValues = &value;
    VkResult r = fns_.WaitSemaphores(device_, &info, timeout_ns);
    if (r == VK_SUCCESS) {
        return true;
    }
    if (r != VK_TIMEOUT) {
        NoteDeviceLost(r);
        ENGINE_VK_CHECK(r);
    }
    return false;
}

VkCommandBuffer VulkanDevice::AcquireUploadCommandBufferLocked() {
    const u64 completed = CompletedTimeline();
    for (usize i = 0; i < upload_buffers_.Size(); ++i) {
        if (upload_buffers_[i].fence <= completed) {
            fns_.ResetCommandBuffer(upload_buffers_[i].buffer, 0);
            upload_buffers_[i].fence = ~0ull; // taken; set to the real value after submit
            return upload_buffers_[i].buffer;
        }
    }
    VkCommandBufferAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool = upload_pool_;
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = 1;
    VkCommandBuffer buffer = nullptr;
    if (ENGINE_VK_CHECK(fns_.AllocateCommandBuffers(device_, &info, &buffer)) != VkResultClass::Success) {
        return nullptr;
    }
    upload_buffers_.Push({buffer, ~0ull});
    return buffer;
}

u64 VulkanDevice::SubmitRawLocked(void (*fill_fn)(void*, VkCommandBuffer), void* fill_context, const VkSemaphoreSubmitInfo* waits,
                                  u32 wait_count, const VkSemaphoreSubmitInfo* extra_signals, u32 extra_signal_count) {
    if (device_lost_.load(std::memory_order_relaxed)) {
        return 0;
    }
    VkCommandBuffer cb = AcquireUploadCommandBufferLocked();
    if (cb == nullptr) {
        return 0;
    }
    auto release = [&]() {
        for (usize i = 0; i < upload_buffers_.Size(); ++i) {
            if (upload_buffers_[i].buffer == cb) {
                upload_buffers_[i].fence = 0;
            }
        }
    };
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (ENGINE_VK_CHECK(fns_.BeginCommandBuffer(cb, &begin)) != VkResultClass::Success) {
        release();
        return 0;
    }
    fill_fn(fill_context, cb);
    if (ENGINE_VK_CHECK(fns_.EndCommandBuffer(cb)) != VkResultClass::Success) {
        release();
        return 0;
    }

    const u64 value = last_submit_value_ + 1;
    VkCommandBufferSubmitInfo cb_info{};
    cb_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cb_info.commandBuffer = cb;
    VkSemaphoreSubmitInfo signals[3]{};
    ENGINE_ASSERT(extra_signal_count <= 2, "SubmitRawLocked: too many extra signals");
    signals[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signals[0].semaphore = timeline_;
    signals[0].value = value;
    signals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    for (u32 i = 0; i < extra_signal_count; ++i) {
        signals[1 + i] = extra_signals[i];
    }
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = wait_count;
    submit.pWaitSemaphoreInfos = waits;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cb_info;
    submit.signalSemaphoreInfoCount = 1 + extra_signal_count;
    submit.pSignalSemaphoreInfos = signals;
    VkResult r = fns_.QueueSubmit2(queue_, 1, &submit, nullptr);
    if (r != VK_SUCCESS) {
        NoteDeviceLost(r);
        ENGINE_VK_CHECK(r);
        release();
        return 0;
    }
    last_submit_value_ = value;
    for (usize i = 0; i < upload_buffers_.Size(); ++i) {
        if (upload_buffers_[i].buffer == cb) {
            upload_buffers_[i].fence = value;
        }
    }
    return value;
}

// ---- buffers ----

BufferHandle VulkanDevice::CreateBuffer(const BufferDesc& desc) {
    const char* why = desc.Validate();
    if (why != nullptr) {
        RhiLog(LogVerbosity::Error, "CreateBuffer: %s", why);
        return {};
    }
    BufferRes res;
    res.desc = desc;
    res.desc.debug_name = nullptr; // the name is not kept
    if (!CreateRawBuffer(desc.size, ToVkBufferUsage(desc.usage), desc.location, "user", &res.raw)) {
        return {};
    }
    SetDebugName(HandleToU64(res.raw.buffer), VK_OBJECT_TYPE_BUFFER, desc.debug_name);
    std::lock_guard<std::mutex> lock(resource_mutex_);
    BufferHandle handle = buffers_.Allocate(static_cast<BufferRes&&>(res));
    if (!handle.IsValid()) {
        RhiLog(LogVerbosity::Error, "CreateBuffer: out of buffer handles");
        DestroyRawBuffer(&res.raw);
    }
    return handle;
}

void VulkanDevice::DestroyBuffer(BufferHandle buffer) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    BufferRes* b = buffers_.Get(buffer);
    ENGINE_ASSERT(b != nullptr, "DestroyBuffer: handle is invalid or already destroyed");
    if (b == nullptr) {
        return;
    }
    ENGINE_ASSERT(!b->mapped, "DestroyBuffer: the buffer is still mapped");
    // Work in the open frame may still use it; outside a frame, the next frame is the earliest that can have used it.
    const u64 current = frame_number_.load(std::memory_order_relaxed);
    const u64 stamp = frame_open_ ? current : current + 1;
    buffers_.Retire(buffer, stamp);
}

void* VulkanDevice::MapBuffer(BufferHandle buffer) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    BufferRes* b = buffers_.Get(buffer);
    ENGINE_ASSERT(b != nullptr, "MapBuffer: handle is invalid or already destroyed");
    if (b == nullptr) {
        return nullptr;
    }
    ENGINE_ASSERT(b->desc.location != MemoryLocation::GpuOnly, "MapBuffer: the buffer is not CPU-visible");
    ENGINE_ASSERT(!b->mapped, "MapBuffer: the buffer is already mapped");
    ENGINE_ASSERT(b->raw.memory.mapped != nullptr, "MapBuffer: the buffer's memory is not mappable");
    b->mapped = true;
    if (b->desc.location == MemoryLocation::GpuToCpu) {
        InvalidateRange(b->raw.memory, 0, b->desc.size);
    }
    return b->raw.memory.mapped;
}

void VulkanDevice::UnmapBuffer(BufferHandle buffer) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    BufferRes* b = buffers_.Get(buffer);
    ENGINE_ASSERT(b != nullptr, "UnmapBuffer: handle is invalid or already destroyed");
    if (b == nullptr) {
        return;
    }
    ENGINE_ASSERT(b->mapped, "UnmapBuffer: the buffer is not mapped");
    if (b->desc.location == MemoryLocation::CpuToGpu) {
        FlushRange(b->raw.memory, 0, b->desc.size);
    }
    b->mapped = false;
}

void VulkanDevice::FlushMappedBuffer(BufferHandle buffer, u64 offset, u64 size) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    BufferRes* b = buffers_.Get(buffer);
    ENGINE_ASSERT(b != nullptr, "FlushMappedBuffer: handle is invalid or already destroyed");
    if (b == nullptr) {
        return;
    }
    ENGINE_ASSERT(offset <= b->desc.size && size <= b->desc.size - offset, "FlushMappedBuffer: the range lies outside the buffer");
    if (b->desc.location == MemoryLocation::CpuToGpu && b->mapped) {
        FlushRange(b->raw.memory, offset, size);
    }
}

UploadTicket VulkanDevice::UploadBuffer(BufferHandle dst, u64 offset, const void* data, u64 size) {
    VkBuffer vk_buffer = nullptr;
    BufferRes snapshot;
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        BufferRes* b = buffers_.Get(dst);
        ENGINE_ASSERT(b != nullptr, "UploadBuffer: handle is invalid or already destroyed");
        if (b == nullptr) {
            return {};
        }
        ENGINE_ASSERT(Has(b->desc.usage, BufferUsage::CopyDst), "UploadBuffer: the buffer was not created with CopyDst usage");
        ENGINE_ASSERT(data != nullptr, "UploadBuffer: data is null");
        ENGINE_ASSERT(size <= b->desc.size && offset <= b->desc.size - size, "UploadBuffer: range outside the buffer");
        snapshot = *b;
        vk_buffer = b->raw.buffer;
    }
    if (size == 0) {
        return {};
    }
    // A CPU-visible destination is written directly through its mapping; no GPU copy, nothing to wait for.
    if (snapshot.desc.location == MemoryLocation::CpuToGpu) {
        std::memcpy(static_cast<char*>(snapshot.raw.memory.mapped) + offset, data, static_cast<usize>(size));
        FlushRange(snapshot.raw.memory, offset, size);
        return {};
    }

    UploadTicket ticket;
    u64 done = 0;
    while (done < size) {
        const u64 chunk = size - done < kUploadChunkBytes ? size - done : kUploadChunkBytes;
        std::lock_guard<std::mutex> lock(submit_mutex_);
        if (device_lost_.load(std::memory_order_relaxed)) {
            ticket.ok = false;
            return ticket;
        }
        staging_ring_->Reclaim(CompletedTimeline());
        u64 ring_offset = 0;
        while (!staging_ring_->Allocate(chunk, 16, &ring_offset)) {
            const u64 pending = staging_ring_->OldestPendingFence();
            if (pending == 0 || !WaitTimeline(pending, kStagingWaitNs)) {
                RhiLog(LogVerbosity::Error, "UploadBuffer: the staging ring is exhausted and the GPU is not freeing it");
                ticket.ok = false;
                return ticket;
            }
            staging_ring_->Reclaim(CompletedTimeline());
        }
        std::memcpy(static_cast<char*>(staging_.memory.mapped) + ring_offset, static_cast<const char*>(data) + done,
                    static_cast<usize>(chunk));
        FlushRange(staging_.memory, ring_offset, chunk);

        const u64 dst_offset = offset + done;
        const u64 value = SubmitInternalLocked([&](VkCommandBuffer cb) {
            // Order the copy after everything submitted earlier that could touch the buffer.
            VkMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.memoryBarrierCount = 1;
            dep.pMemoryBarriers = &barrier;
            fns_.CmdPipelineBarrier2(cb, &dep);
            VkBufferCopy region{ring_offset, dst_offset, chunk};
            fns_.CmdCopyBuffer(cb, staging_.buffer, vk_buffer, 1, &region);
        });
        if (value == 0) {
            // The chunk's staging space was never submitted; let it be reclaimed with the last real submission.
            staging_ring_->Retire(last_submit_value_);
            ticket.ok = false;
            return ticket;
        }
        staging_ring_->Retire(value);
        ticket.fence = value;
        done += chunk;
    }
    return ticket;
}

bool VulkanDevice::ReadbackBuffer(BufferHandle src, u64 offset, void* dst, u64 size) {
    BufferRes snapshot;
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        BufferRes* b = buffers_.Get(src);
        ENGINE_ASSERT(b != nullptr, "ReadbackBuffer: handle is invalid or already destroyed");
        if (b == nullptr) {
            return false;
        }
        ENGINE_ASSERT(Has(b->desc.usage, BufferUsage::CopySrc) || b->desc.location == MemoryLocation::GpuToCpu,
                      "ReadbackBuffer: the buffer was not created with CopySrc usage");
        ENGINE_ASSERT(dst != nullptr, "ReadbackBuffer: destination is null");
        ENGINE_ASSERT(size <= b->desc.size && offset <= b->desc.size - size, "ReadbackBuffer: range outside the buffer");
        snapshot = *b;
    }
    if (size == 0) {
        return true;
    }
    if (device_lost_.load(std::memory_order_relaxed)) {
        return false;
    }

    if (snapshot.desc.location != MemoryLocation::GpuOnly) {
        // Everything submitted so far must be finished before the CPU reads the mapping. (The GPU-only path below
        // needs no such wait: its copy is ordered after earlier work on the queue.)
        u64 last = 0;
        {
            std::lock_guard<std::mutex> lock(submit_mutex_);
            last = last_submit_value_;
        }
        if (!WaitTimeline(last, ~0ull)) {
            return false;
        }
        InvalidateRange(snapshot.raw.memory, offset, size);
        std::memcpy(dst, static_cast<const char*>(snapshot.raw.memory.mapped) + offset, static_cast<usize>(size));
        return true;
    }

    u64 done = 0;
    while (done < size) {
        const u64 chunk = size - done < kReadbackChunkBytes ? size - done : kReadbackChunkBytes;
        RawBuffer temp;
        if (!CreateRawBuffer(chunk, VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryLocation::GpuToCpu, "readback", &temp) ||
            temp.memory.mapped == nullptr) {
            DestroyRawBuffer(&temp);
            return false;
        }
        u64 value = 0;
        {
            std::lock_guard<std::mutex> lock(submit_mutex_);
            value = SubmitInternalLocked([&](VkCommandBuffer cb) {
                VkMemoryBarrier2 before{};
                before.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
                before.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                before.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                before.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
                before.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
                VkDependencyInfo dep_before{};
                dep_before.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dep_before.memoryBarrierCount = 1;
                dep_before.pMemoryBarriers = &before;
                fns_.CmdPipelineBarrier2(cb, &dep_before);
                VkBufferCopy region{offset + done, 0, chunk};
                fns_.CmdCopyBuffer(cb, snapshot.raw.buffer, temp.buffer, 1, &region);
                VkMemoryBarrier2 after{};
                after.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
                after.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
                after.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                after.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
                after.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
                VkDependencyInfo dep_after{};
                dep_after.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dep_after.memoryBarrierCount = 1;
                dep_after.pMemoryBarriers = &after;
                fns_.CmdPipelineBarrier2(cb, &dep_after);
            });
        }
        if (value == 0 || !WaitTimeline(value, ~0ull)) {
            DestroyRawBuffer(&temp);
            return false;
        }
        InvalidateRange(temp.memory, 0, chunk);
        std::memcpy(static_cast<char*>(dst) + done, temp.memory.mapped, static_cast<usize>(chunk));
        DestroyRawBuffer(&temp);
        done += chunk;
    }
    return true;
}

// ---- fences and frames ----

bool VulkanDevice::WaitFence(FenceValue value, u64 timeout_ns) {
    if (value == 0) {
        return true;
    }
    if (device_lost_.load(std::memory_order_relaxed)) {
        return false;
    }
    return WaitTimeline(value, timeout_ns);
}

FenceValue VulkanDevice::CompletedFence() const { return CompletedTimeline(); }

void VulkanDevice::UpdateCompletedFrames() {
    const u64 completed = CompletedTimeline();
    while (completed_frames_ < ended_frames_ && frame_fences_[(completed_frames_ + 1) % kTrackedFrames] <= completed) {
        ++completed_frames_;
    }
}

void VulkanDevice::CollectRetired(bool all) {
    const u64 completed = all ? ~0ull : completed_frames_;
    buffers_.CollectRetired(completed, [&](BufferRes& b) { DestroyRawBuffer(&b.raw); });
    textures_.CollectRetired(completed, [&](TextureRes& t) { DestroyTextureRes(&t); });
    samplers_.CollectRetired(completed, [&](SamplerRes& s) { fns_.DestroySampler(device_, s.sampler, nullptr); });
    shaders_.CollectRetired(completed, [&](ShaderRes& s) { fns_.DestroyShaderModule(device_, s.module, nullptr); });
    bind_groups_.CollectRetired(completed, [&](BindGroupRes& g) { ReleaseBindGroupLocked(&g); });
    pipelines_.CollectRetired(completed, [&](PipelineRes& p) {
        fns_.DestroyPipeline(device_, p.pipeline, nullptr);
        ReleasePipelineLayoutLocked(p.layout_slot);
    });
    bind_group_layouts_.CollectRetired(completed, [&](BindGroupLayoutRes& l) { ReleaseSetLayoutLocked(l.set_slot); });
}

u64 VulkanDevice::CompletedFrame() const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    return completed_frames_;
}

VulkanDevice::ObjectCounts VulkanDevice::Counts() const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    ObjectCounts counts;
    counts.buffers = buffers_.LiveCount();
    counts.pending_destruction = buffers_.RetiredCount();
    counts.raw_buffers = raw_buffer_count_.load(std::memory_order_relaxed);
    counts.textures = textures_.LiveCount();
    counts.samplers = samplers_.LiveCount();
    counts.pending_image_destruction = textures_.RetiredCount() + samplers_.RetiredCount();
    counts.raw_images = raw_image_count_.load(std::memory_order_relaxed);
    counts.shaders = shaders_.LiveCount();
    counts.bind_group_layouts = bind_group_layouts_.LiveCount();
    counts.pipelines = pipelines_.LiveCount();
    counts.bind_groups = bind_groups_.LiveCount();
    counts.descriptor_pools = persistent_pools_.pools.Size() + persistent_pools_.exact.Size();
    for (const DescriptorPools& frame : frame_pools_) {
        counts.descriptor_pools += frame.pools.Size() + frame.exact.Size();
    }
    for (const SetLayoutEntry& e : set_layouts_) {
        counts.descriptor_set_layouts += e.layout != nullptr ? 1 : 0;
    }
    for (const PipelineLayoutEntry& e : pipeline_layouts_) {
        counts.pipeline_layouts += e.layout != nullptr ? 1 : 0;
    }
    return counts;
}

void VulkanDevice::BeginFrame() {
    // Never let the unfinished frames outrun the fence ring, and never reuse a command pool the GPU may still be
    // reading. The wait happens outside the lock, so other threads can keep creating and destroying resources meanwhile.
    bool waited = true;
    for (;;) {
        u64 wait_for = 0;
        {
            std::lock_guard<std::mutex> lock(resource_mutex_);
            UpdateCompletedFrames();
            if (ended_frames_ - completed_frames_ >= kTrackedFrames - 2) {
                wait_for = frame_fences_[(completed_frames_ + 1) % kTrackedFrames];
            }
            const u64 pool_frame = frame_number_ + 1 > frames_in_flight_ ? frame_number_ + 1 - frames_in_flight_ : 0;
            if (pool_frame > completed_frames_ && pool_frame <= ended_frames_) {
                const u64 fence = frame_fences_[pool_frame % kTrackedFrames];
                wait_for = wait_for > fence ? wait_for : fence;
            }
        }
        if (wait_for == 0) {
            break;
        }
        if (!WaitTimeline(wait_for, ~0ull)) {
            waited = false; // the device is lost; the GPU may still be reading, so no pool is reset below
            break;
        }
    }
    u64 frame = 0;
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        ENGINE_ASSERT(!frame_open_, "BeginFrame: the previous frame was not ended");
        UpdateCompletedFrames();
        frame = frame_number_.load(std::memory_order_relaxed) + 1;
        frame_number_.store(frame, std::memory_order_relaxed);
        frame_open_ = true;
        if (lose_device_at_frame_ != 0 && frame == lose_device_at_frame_ && !device_lost_.exchange(true, std::memory_order_relaxed)) {
            RhiLog(LogVerbosity::Error, "Vulkan: the device was lost (injected at frame %llu); further submissions are ignored",
                   static_cast<unsigned long long>(frame));
            ENGINE_ENSURE(false, "the Vulkan device was lost"); // the same report as a real loss: a trace, once
        }
        if (waited && !device_lost_.load(std::memory_order_relaxed)) {
            ResetFrameDescriptorsLocked(frame % kFramePools); // the frame that last used these pools has finished
        }
        CollectRetired(false);
    }
    // The frame that last used these pools has finished (waited for above), so they can be reset. This is outside the
    // lock: it makes a driver call per pool, and no list of this frame exists yet.
    if (waited && !device_lost_.load(std::memory_order_relaxed)) {
        ResetFramePools(frame % kFramePools);
    }
}

void VulkanDevice::EndFrame() {
    u64 frame = 0;
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        ENGINE_ASSERT(frame_open_, "EndFrame: no frame is open");
        frame_open_ = false; // later destroys now belong to the next frame
        frame = frame_number_;
    }
    // Read the last submission after closing the frame: a buffer destroyed in this frame may have been used by any
    // submission made before the close, and this value covers all of them.
    u64 last = 0;
    {
        std::lock_guard<std::mutex> lock(submit_mutex_);
        last = last_submit_value_;
    }
    std::lock_guard<std::mutex> lock(resource_mutex_);
    ended_frames_ = frame;
    frame_fences_[frame % kTrackedFrames] = last;
}

u64 VulkanDevice::FrameNumber() const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    return frame_number_;
}

void VulkanDevice::WaitIdle() {
    NoteDeviceLost(fns_.DeviceWaitIdle(device_));
    std::lock_guard<std::mutex> lock(resource_mutex_);
    UpdateCompletedFrames();
    CollectRetired(true); // the device is idle, so everything retired is free to release
}

} // namespace engine_placeholder::rhi::vulkan
