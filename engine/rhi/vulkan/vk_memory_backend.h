#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>

#include "rhi/vulkan/vk_functions.h"
#include "rhi/vulkan/vk_memory.h"

//! The Vulkan source of memory objects for [[rhi::vulkan::MemoryAllocator]].

namespace engine_placeholder::rhi::vulkan {

/// Converts a non-dispatchable Vulkan handle to the opaque 64-bit value the allocator stores.
template <typename H>
inline u64 HandleToU64(H handle) {
    if constexpr (sizeof(H) == sizeof(u64) && !std::is_pointer_v<H>) {
        return static_cast<u64>(handle);
    } else {
        return static_cast<u64>(reinterpret_cast<uintptr_t>(handle));
    }
}

/// Converts the allocator's opaque value back to a non-dispatchable Vulkan handle.
template <typename H>
inline H HandleFromU64(u64 value) {
    if constexpr (sizeof(H) == sizeof(u64) && !std::is_pointer_v<H>) {
        return static_cast<H>(value);
    } else {
        return reinterpret_cast<H>(static_cast<uintptr_t>(value));
    }
}

/// Allocates and frees `VkDeviceMemory` for the allocator, mapping host-visible objects once.
///
/// Thread-safety: thread-compatible; the allocator serializes calls. The counters are atomic.
/// Ownership: borrows the device table and device, which must outlive it.
class VulkanMemoryBackend final : public MemoryBackend {
public:
    /// Creates a backend for `device`.
    ///
    /// Param fns: the device function table; must outlive the backend.
    /// Param device: the logical device.
    /// Param device_lost: set to true when an allocation reports a lost device; may be null.
    VulkanMemoryBackend(const VkDeviceFns* fns, VkDevice device, std::atomic<bool>* device_lost);

    MemoryResult Allocate(u32 memory_type, u64 size, bool map, u64 dedicated_buffer, u64 dedicated_image,
                          BackendAllocation* out) override;
    void Free(const BackendAllocation& allocation) override;

    /// Makes the `n`th call to `Allocate` (1-based, counting every attempt) and all later ones fail with
    /// `MemoryResult::OutOfDeviceMemory` without calling the driver; 0 turns it off. For failure-injection tests.
    void FailAllocationsFrom(u32 n) { fail_from_.store(n, std::memory_order_relaxed); }

    /// Returns how many times `vkAllocateMemory` succeeded.
    u64 AllocateCalls() const { return allocate_calls_.load(std::memory_order_relaxed); }
    /// Returns how many memory objects are currently allocated.
    u64 LiveObjects() const { return live_.load(std::memory_order_relaxed); }

private:
    const VkDeviceFns* fns_;
    VkDevice device_;
    std::atomic<bool>* device_lost_;
    std::atomic<u32> fail_from_{0};
    std::atomic<u64> attempts_{0};
    std::atomic<u64> allocate_calls_{0};
    std::atomic<u64> live_{0};
};

} // namespace engine_placeholder::rhi::vulkan
