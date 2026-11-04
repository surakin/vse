#include "rhi/vulkan/vk_memory_backend.h"

#include "rhi/vulkan/vk_result.h"

namespace engine_placeholder::rhi::vulkan {

VulkanMemoryBackend::VulkanMemoryBackend(const VkDeviceFns* fns, VkDevice device, std::atomic<bool>* device_lost)
    : fns_(fns), device_(device), device_lost_(device_lost) {}

MemoryResult VulkanMemoryBackend::Allocate(u32 memory_type, u64 size, bool map, u64 dedicated_buffer, u64 dedicated_image,
                                           BackendAllocation* out) {
    const u64 attempt = attempts_.fetch_add(1, std::memory_order_relaxed) + 1;
    const u32 fail_from = fail_from_.load(std::memory_order_relaxed);
    if (fail_from != 0 && attempt >= fail_from) {
        return MemoryResult::OutOfDeviceMemory; // injected
    }
    VkMemoryDedicatedAllocateInfo dedicated{};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated.buffer = HandleFromU64<VkBuffer>(dedicated_buffer);
    dedicated.image = HandleFromU64<VkImage>(dedicated_image);

    VkMemoryAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    info.pNext = (dedicated_buffer != 0 || dedicated_image != 0) ? &dedicated : nullptr;
    info.allocationSize = size;
    info.memoryTypeIndex = memory_type;

    VkDeviceMemory memory = nullptr;
    VkResult result = fns_->AllocateMemory(device_, &info, nullptr, &memory);
    if (result != VK_SUCCESS) {
        if (result == VK_ERROR_DEVICE_LOST && device_lost_ != nullptr) {
            device_lost_->store(true, std::memory_order_relaxed);
        }
        // Failures here are expected under memory pressure; the allocator logs them with the heap breakdown.
        if (result == VK_ERROR_OUT_OF_DEVICE_MEMORY) {
            return MemoryResult::OutOfDeviceMemory;
        }
        if (result == VK_ERROR_OUT_OF_HOST_MEMORY) {
            return MemoryResult::OutOfHostMemory;
        }
        ENGINE_VK_CHECK(result);
        return MemoryResult::Failed;
    }

    void* mapped = nullptr;
    if (map) {
        result = fns_->MapMemory(device_, memory, 0, VK_WHOLE_SIZE, 0, &mapped);
        if (result != VK_SUCCESS) {
            fns_->FreeMemory(device_, memory, nullptr);
            if (result == VK_ERROR_DEVICE_LOST && device_lost_ != nullptr) {
                device_lost_->store(true, std::memory_order_relaxed);
            }
            if (result == VK_ERROR_OUT_OF_HOST_MEMORY) {
                return MemoryResult::OutOfHostMemory;
            }
            if (result == VK_ERROR_OUT_OF_DEVICE_MEMORY) {
                return MemoryResult::OutOfDeviceMemory;
            }
            ENGINE_VK_CHECK(result);
            return MemoryResult::Failed;
        }
    }
    allocate_calls_.fetch_add(1, std::memory_order_relaxed);
    live_.fetch_add(1, std::memory_order_relaxed);
    out->handle = HandleToU64(memory);
    out->mapped = mapped;
    return MemoryResult::Ok;
}

void VulkanMemoryBackend::Free(const BackendAllocation& allocation) {
    VkDeviceMemory memory = HandleFromU64<VkDeviceMemory>(allocation.handle);
    if (allocation.mapped != nullptr) {
        fns_->UnmapMemory(device_, memory);
    }
    fns_->FreeMemory(device_, memory, nullptr);
    live_.fetch_sub(1, std::memory_order_relaxed);
}

} // namespace engine_placeholder::rhi::vulkan
