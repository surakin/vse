#pragma once

#include <atomic>

#include "rhi/device.h"
#include "rhi/rhi.h"
#include "rhi/vulkan/vk_device_select.h"
#include "rhi/vulkan/vk_functions.h"
#include "rhi/vulkan/vk_instance.h"

//! The Vulkan implementation of [[rhi::Device]].

namespace engine_placeholder::rhi::vulkan {

/// The Vulkan 1.3 device: one physical GPU, one logical device and one graphics+compute queue.
///
/// Created by [[rhi::CreateDevice]] (or [[rhi::vulkan::VulkanDevice::Create]]). All work, including uploads, runs
/// on the single graphics queue; a dedicated transfer family is recorded in the queue plan but unused in Phase 3.
///
/// Thread-safety: thread-safe for creation, destruction and mapping, as for [[rhi::Device]]; `Submit`, `BeginFrame`,
///   `EndFrame` and `WaitIdle` belong to one submission thread.
/// Ownership: the caller owns the device and releases it with [[rhi::DestroyDevice]]; it owns the Vulkan instance.
class VulkanDevice final : public Device {
public:
    /// Creates a Vulkan device.
    ///
    /// Param info: what to create; the backend field is ignored.
    /// Returns: the device, or null.
    /// Fails: as for [[rhi::CreateDevice]].
    /// Ownership: the caller owns the result.
    static VulkanDevice* Create(const DeviceCreateInfo& info);

    /// Waits for the GPU to go idle, then destroys every resource, the device and the instance.
    ~VulkanDevice() override;

    const char* BackendName() const override;
    const DeviceCaps& Caps() const override;
    BufferHandle CreateBuffer(const BufferDesc& desc) override;
    void DestroyBuffer(BufferHandle buffer) override;
    TextureHandle CreateTexture(const TextureDesc& desc) override;
    void DestroyTexture(TextureHandle texture) override;
    SamplerHandle CreateSampler(const SamplerDesc& desc) override;
    void DestroySampler(SamplerHandle sampler) override;
    ShaderHandle CreateShader(const ShaderDesc& desc) override;
    void DestroyShader(ShaderHandle shader) override;
    BindGroupLayoutHandle CreateBindGroupLayout(const BindGroupLayoutDesc& desc) override;
    void DestroyBindGroupLayout(BindGroupLayoutHandle layout) override;
    BindGroupHandle CreateBindGroup(const BindGroupDesc& desc) override;
    void DestroyBindGroup(BindGroupHandle group) override;
    PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    PipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc) override;
    void DestroyPipeline(PipelineHandle pipeline) override;
    void* MapBuffer(BufferHandle buffer) override;
    void UnmapBuffer(BufferHandle buffer) override;
    CommandList* CreateCommandList() override;
    FenceValue Submit(CommandList* const* lists, usize count) override;
    bool WaitFence(FenceValue value, u64 timeout_ns) override;
    FenceValue CompletedFence() const override;
    void BeginFrame() override;
    void EndFrame() override;
    u64 FrameNumber() const override;
    void WaitIdle() override;
    bool DeviceLost() const override;

    /// Returns the name of the GPU in use, as reported by the driver.
    /// Ownership: the string lives as long as the device.
    const char* GpuName() const { return gpu_info_.name; }

    /// Returns the description of the GPU in use.
    const PhysicalDeviceInfo& GpuInfo() const { return gpu_info_; }

    /// Returns the queue families in use.
    const QueuePlan& Queues() const { return queues_; }

    /// Returns the instance this device was created from.
    const VulkanInstance& Instance() const { return instance_; }

    /// Returns the raw `VkDevice`. Ownership: borrowed; valid until the device is destroyed.
    VkDevice Handle() const { return device_; }

    /// Returns the device-level function table.
    const VkDeviceFns& Fns() const { return fns_; }

    /// Returns the physical device's memory properties.
    const VkPhysicalDeviceMemoryProperties& MemoryProperties() const { return memory_properties_; }

    /// Returns the physical device's properties and limits.
    const VkPhysicalDeviceProperties& Properties() const { return properties_; }

private:
    VulkanDevice() = default;
    bool Initialize(const DeviceCreateInfo& info);
    void FillCaps();
    // Logs that `what` is not implemented yet in this build of the backend (resource kinds arrive on later days).
    static void NotImplemented(const char* what);

    VulkanInstance instance_;
    VkPhysicalDevice physical_ = nullptr;
    VkDevice device_ = nullptr;
    VkDeviceFns fns_;
    VkQueue queue_ = nullptr;
    PhysicalDeviceInfo gpu_info_;
    QueuePlan queues_;
    VkPhysicalDeviceProperties properties_{};
    VkPhysicalDeviceMemoryProperties memory_properties_{};
    DeviceCaps caps_;
    bool anisotropy_enabled_ = false;
    std::atomic<bool> device_lost_{false};
    u64 frame_number_ = 0;
};

} // namespace engine_placeholder::rhi::vulkan
