// VulkanDevice: per-thread command pools, command list creation and submission.

#include <atomic>
#include <mutex>

#include "rhi/rhi_log.h"
#include "rhi/vulkan/vk_result.h"
#include "rhi/vulkan/vulkan_device.h"

namespace engine_placeholder::rhi::vulkan {

namespace {

// Every thread that records gets a slot from a process-wide table and gives it back when it exits, so a program
// that starts and stops threads does not run out. (A job participant index would not do: indices are only unique
// within one worker pool, and pools may coexist.) The table is a constant-initialized array of atomic bitmasks with
// no destructor, so a thread's exit handler can run after static destructors and still find it intact.
constexpr usize kSlotWords = (VulkanDevice::kThreadSlots + 63) / 64;
std::atomic<u64> g_slot_words[kSlotWords];

struct FallbackSlotHolder {
    i32 id = -1;
    ~FallbackSlotHolder() {
        if (id >= 0) {
            g_slot_words[static_cast<usize>(id) / 64].fetch_and(~(u64{1} << (static_cast<usize>(id) % 64)), std::memory_order_release);
        }
    }
};

// Claims a free bit in the table; returns its index, or -1 if all are taken.
i32 ClaimSlot() {
    for (usize w = 0; w < kSlotWords; ++w) {
        u64 bits = g_slot_words[w].load(std::memory_order_relaxed);
        for (;;) {
            usize bit = 0;
            while (bit < 64 && (w * 64 + bit) < VulkanDevice::kThreadSlots && ((bits >> bit) & 1) != 0) {
                ++bit;
            }
            if (bit == 64 || (w * 64 + bit) >= VulkanDevice::kThreadSlots) {
                break; // this word is full
            }
            if (g_slot_words[w].compare_exchange_weak(bits, bits | (u64{1} << bit), std::memory_order_acquire, std::memory_order_relaxed)) {
                return static_cast<i32>(w * 64 + bit);
            }
        }
    }
    return -1;
}

// Returns the calling thread's pool slot, or -1 if all are taken.
i32 CurrentPoolSlot() {
    thread_local FallbackSlotHolder holder;
    if (holder.id < 0) {
        holder.id = ClaimSlot();
    }
    return holder.id;
}

} // namespace

bool VulkanDevice::LookupBuffer(BufferHandle buffer, VkBuffer* vk_buffer, BufferDesc* desc) const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    const BufferRes* b = buffers_.Get(buffer);
    if (b == nullptr) {
        return false;
    }
    *vk_buffer = b->raw.buffer;
    *desc = b->desc;
    return true;
}

void VulkanDevice::ReportResult(VkResult result) {
    NoteDeviceLost(result);
    ENGINE_VK_CHECK(result);
}

usize VulkanDevice::CommandPoolCount() const {
    usize count = 0;
    for (usize f = 0; f < kFramePools; ++f) {
        for (usize s = 0; s < kThreadSlots; ++s) {
            if (thread_pools_[f][s].load(std::memory_order_acquire) != nullptr) {
                ++count;
            }
        }
    }
    return count;
}

VulkanDevice::ThreadPool* VulkanDevice::PoolForCurrentThread(usize frame_slot) {
    const i32 slot = CurrentPoolSlot();
    if (slot < 0) {
        RhiLog(LogVerbosity::Error, "Vulkan: no command pool slot is free for this thread (more than %zu threads recording at once)",
               kThreadSlots);
        return nullptr;
    }
    std::atomic<ThreadPool*>& entry = thread_pools_[frame_slot][slot];
    ThreadPool* pool = entry.load(std::memory_order_acquire);
    if (pool != nullptr) {
        return pool;
    }
    VkCommandPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.queueFamilyIndex = static_cast<u32>(queues_.graphics_family);
    VkCommandPool vk_pool = nullptr;
    const VkResult r = fns_.CreateCommandPool(device_, &info, nullptr, &vk_pool);
    if (r != VK_SUCCESS) {
        ReportResult(r);
        return nullptr;
    }
    UniquePtr<ThreadPool> made = MakeUnique<ThreadPool>();
    if (!made) {
        fns_.DestroyCommandPool(device_, vk_pool, nullptr);
        return nullptr;
    }
    made->pool = vk_pool;
    pool = made.Get();
    owned_pools_[frame_slot][slot] = std::move(made);
    entry.store(pool, std::memory_order_release);
    return pool;
}

void VulkanDevice::ResetFramePools(usize frame_slot) {
    for (usize s = 0; s < kThreadSlots; ++s) {
        ThreadPool* pool = thread_pools_[frame_slot][s].load(std::memory_order_acquire);
        if (pool == nullptr) {
            continue;
        }
        ReportResult(fns_.ResetCommandPool(device_, pool->pool, 0));
        pool->used = 0;
    }
}

void VulkanDevice::ShutdownCommands() {
    for (usize f = 0; f < kFramePools; ++f) {
        for (usize s = 0; s < kThreadSlots; ++s) {
            ThreadPool* pool = thread_pools_[f][s].exchange(nullptr);
            if (pool == nullptr) {
                continue;
            }
            pool->lists.Reset();
            fns_.DestroyCommandPool(device_, pool->pool, nullptr); // frees its command buffers
            owned_pools_[f][s].Reset();
        }
    }
}

CommandList* VulkanDevice::CreateCommandList() {
    ENGINE_ASSERT(frame_open_, "CreateCommandList: no frame is open (call BeginFrame first)");
    if (device_lost_.load(std::memory_order_relaxed)) {
        return nullptr;
    }
    const u64 frame = frame_number_.load(std::memory_order_relaxed);
    ThreadPool* pool = PoolForCurrentThread(frame % kFramePools);
    if (pool == nullptr) {
        return nullptr;
    }
    if (pool->used == pool->lists.Size()) {
        VkCommandBufferAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        info.commandPool = pool->pool;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = 1;
        VkCommandBuffer cb = nullptr;
        const VkResult r = fns_.AllocateCommandBuffers(device_, &info, &cb);
        if (r != VK_SUCCESS) {
            ReportResult(r);
            return nullptr;
        }
        UniquePtr<VulkanCommandList> fresh = MakeUnique<VulkanCommandList>(this, cb);
        if (!fresh) {
            return nullptr; // the buffer goes back to the pool with the pool
        }
        pool->lists.Push(std::move(fresh));
    }
    VulkanCommandList* list = pool->lists[pool->used++].Get();
    list->Recycle(frame);
    return list;
}

FenceValue VulkanDevice::Submit(CommandList* const* lists, usize count) {
    ENGINE_ASSERT(frame_open_, "Submit: no frame is open (call BeginFrame first)");
    ENGINE_ASSERT(count == 0 || lists != nullptr, "Submit: lists is null but count is not zero");
    const u64 frame = frame_number_.load(std::memory_order_relaxed);

    // Everything below is under the submission mutex, so two threads that wrongly submit the same list are
    // serialized and the second one aborts instead of racing.
    std::lock_guard<std::mutex> lock(submit_mutex_);
    // Check the whole batch before changing anything; a list in the batch twice is caught by the Submitting phase.
    for (usize i = 0; i < count; ++i) {
        auto* list = static_cast<VulkanCommandList*>(lists[i]);
        ENGINE_ASSERT(list != nullptr && list->Owner() == this, "Submit: a list is null or belongs to another device");
        ENGINE_ASSERT(list->GetPhase() == VulkanCommandList::Phase::Ended, "Submit: a list was not ended, or was already submitted or listed twice");
        ENGINE_ASSERT(list->Frame() == frame, "Submit: a list is from an earlier frame");
        list->SetPhase(VulkanCommandList::Phase::Submitting);
    }

    auto finish = [&](VulkanCommandList::Phase phase) {
        for (usize i = 0; i < count; ++i) {
            static_cast<VulkanCommandList*>(lists[i])->SetPhase(phase);
        }
    };
    if (device_lost_.load(std::memory_order_relaxed)) {
        finish(VulkanCommandList::Phase::Submitted);
        return 0;
    }
    if (count == 0) {
        return last_submit_value_; // nothing new: the fence for everything submitted so far
    }
    for (usize i = 0; i < count; ++i) {
        if (static_cast<VulkanCommandList*>(lists[i])->Failed()) {
            RhiLog(LogVerbosity::Error, "Submit: list %zu failed while recording and was not submitted (nothing in the batch ran)", i);
            finish(VulkanCommandList::Phase::Submitted);
            return 0;
        }
    }

    submit_scratch_.Clear();
    for (usize i = 0; i < count; ++i) {
        VkCommandBufferSubmitInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cb.commandBuffer = static_cast<VulkanCommandList*>(lists[i])->Buffer();
        submit_scratch_.Push(cb);
    }
    const u64 value = last_submit_value_ + 1;
    VkSemaphoreSubmitInfo signal{};
    signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal.semaphore = timeline_;
    signal.value = value;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = static_cast<u32>(count);
    submit.pCommandBufferInfos = &submit_scratch_[0];
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;

    const i32 injected = inject_submit_result_.exchange(0, std::memory_order_relaxed);
    const VkResult r = injected != 0 ? static_cast<VkResult>(injected) : fns_.QueueSubmit2(queue_, 1, &submit, nullptr);
    finish(VulkanCommandList::Phase::Submitted);
    if (r != VK_SUCCESS) {
        ReportResult(r);
        return 0;
    }
    last_submit_value_ = value;
    return value;
}

} // namespace engine_placeholder::rhi::vulkan
