#include "rhi/vulkan/vulkan_device.h"

#include <cstdlib>
#include <cstring>

#include "rhi/rhi_log.h"
#include "rhi/vulkan/vk_format.h"
#include "rhi/vulkan/vk_result.h"

namespace engine_placeholder::rhi::vulkan {

namespace {

GpuKind ToGpuKind(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return GpuKind::Discrete;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return GpuKind::Integrated;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return GpuKind::Virtual;
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return GpuKind::Cpu;
    default: return GpuKind::Other;
    }
}

// Reads everything the selection policy needs about one physical device.
void DescribeDevice(const VkInstanceFns& fns, VkPhysicalDevice device, PhysicalDeviceInfo* out) {
    *out = PhysicalDeviceInfo{};

    VkPhysicalDeviceProperties props{};
    fns.GetPhysicalDeviceProperties(device, &props);
    std::strncpy(out->name, props.deviceName, sizeof(out->name) - 1);
    out->kind = ToGpuKind(props.deviceType);
    out->api_version = props.apiVersion;

    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceVulkan12Features f12{};
    f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    f12.pNext = &f13;
    VkPhysicalDeviceFeatures2 f2{};
    f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    f2.pNext = &f12;
    // Features of a pre-1.3 device are queried only through the structs it understands; the version check rejects it.
    if (props.apiVersion >= VK_API_VERSION_1_3) {
        fns.GetPhysicalDeviceFeatures2(device, &f2);
        out->dynamic_rendering = f13.dynamicRendering == VK_TRUE;
        out->synchronization2 = f13.synchronization2 == VK_TRUE;
        out->timeline_semaphore = f12.timelineSemaphore == VK_TRUE;
        out->sampler_anisotropy = f2.features.samplerAnisotropy == VK_TRUE;
    }

    VkQueueFamilyProperties families[kMaxQueueFamilies];
    u32 family_count = static_cast<u32>(kMaxQueueFamilies);
    fns.GetPhysicalDeviceQueueFamilyProperties(device, &family_count, families);
    out->queue_family_count = family_count;
    for (u32 i = 0; i < family_count; ++i) {
        QueueFamilyInfo& f = out->families[i];
        f.queue_count = families[i].queueCount;
        f.graphics = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        f.compute = (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        f.transfer = (families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) != 0;
        f.timestamp_valid_bits = families[i].timestampValidBits;
    }

    u32 ext_count = 0;
    if (fns.EnumerateDeviceExtensionProperties(device, nullptr, &ext_count, nullptr) == VK_SUCCESS && ext_count > 0) {
        VkExtensionProperties* exts = new VkExtensionProperties[ext_count];
        VkResult listed = fns.EnumerateDeviceExtensionProperties(device, nullptr, &ext_count, exts);
        if (listed == VK_SUCCESS || listed == VK_INCOMPLETE) {
            for (u32 i = 0; i < ext_count; ++i) {
                if (std::strcmp(exts[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                    out->swapchain_extension = true;
                }
                if (std::strcmp(exts[i].extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0) {
                    out->memory_budget_extension = true;
                }
            }
        }
        delete[] exts;
    }
}

} // namespace

VulkanDevice* VulkanDevice::Create(const DeviceCreateInfo& info) {
    VulkanDevice* device = new VulkanDevice();
    if (!device->Initialize(info)) {
        delete device;
        return nullptr;
    }
    return device;
}

bool VulkanDevice::Initialize(const DeviceCreateInfo& info) {
    VulkanLoader loader;
    if (LoadVulkanLoader(&loader) != LoaderStatus::Ok) {
        return false;
    }

    VulkanInstanceDesc instance_desc;
    instance_desc.application_name = info.application_name;
    instance_desc.validation = info.validation == ValidationSetting::Off  ? ValidationMode::Off
                               : info.validation == ValidationSetting::On ? ValidationMode::On
                                                                           : ValidationMode::Auto;
    instance_desc.sync_validation = info.sync_validation;
    instance_ = VulkanInstance::Create(loader, instance_desc);
    if (!instance_.Valid()) {
        return false;
    }
    const VkInstanceFns& ifns = instance_.Fns();
    VkInstance instance = instance_.Handle();

    // Enumerate and describe every physical device.
    VkPhysicalDevice physicals[kMaxPhysicalDevices];
    u32 physical_count = static_cast<u32>(kMaxPhysicalDevices);
    VkResult enumerated = ifns.EnumeratePhysicalDevices(instance, &physical_count, physicals);
    if ((enumerated != VK_SUCCESS && enumerated != VK_INCOMPLETE) || physical_count == 0) {
        RhiLog(LogVerbosity::Error, "Vulkan: no physical devices found");
        return false;
    }
    PhysicalDeviceInfo infos[kMaxPhysicalDevices];
    for (u32 i = 0; i < physical_count; ++i) {
        DescribeDevice(ifns, physicals[i], &infos[i]);
    }

    const bool presentable = false;
    int chosen = SelectPhysicalDevice(infos, physical_count, presentable, std::getenv("ENGINE_RHI_GPU"));
    if (chosen < 0) {
        return false;
    }
    physical_ = physicals[chosen];
    gpu_info_ = infos[chosen];
    queues_ = PlanQueues(gpu_info_, presentable);

    ifns.GetPhysicalDeviceProperties(physical_, &properties_);
    ifns.GetPhysicalDeviceMemoryProperties(physical_, &memory_properties_);

    // Enable exactly the features the backend uses.
    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features f12{};
    f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    f12.pNext = &f13;
    f12.timelineSemaphore = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2{};
    f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    f2.pNext = &f12;
    anisotropy_enabled_ = gpu_info_.sampler_anisotropy;
    f2.features.samplerAnisotropy = anisotropy_enabled_ ? VK_TRUE : VK_FALSE;

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = static_cast<u32>(queues_.graphics_family);
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    const char* extensions[2];
    u32 extension_count = 0;
    memory_budget_enabled_ = gpu_info_.memory_budget_extension;
    if (memory_budget_enabled_) {
        extensions[extension_count++] = VK_EXT_MEMORY_BUDGET_EXTENSION_NAME;
    }

    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.enabledExtensionCount = extension_count;
    device_info.ppEnabledExtensionNames = extensions;
    device_info.pNext = &f2;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    if (ENGINE_VK_CHECK(ifns.CreateDevice(physical_, &device_info, nullptr, &device_)) != VkResultClass::Success) {
        device_ = nullptr;
        return false;
    }
    if (!LoadDeviceFns(ifns, device_, &fns_)) {
        // Cannot destroy through our table; use the loader's pointer directly.
        PFN_vkDestroyDevice destroy = reinterpret_cast<PFN_vkDestroyDevice>(ifns.GetDeviceProcAddr(device_, "vkDestroyDevice"));
        if (destroy != nullptr) {
            destroy(device_, nullptr);
        }
        device_ = nullptr;
        return false;
    }
    fns_.GetDeviceQueue(device_, static_cast<u32>(queues_.graphics_family), 0, &queue_);

    FillCaps();

    // The suballocator, over the device's real memory layout.
    MemoryLayout layout;
    layout.type_count = memory_properties_.memoryTypeCount;
    for (u32 i = 0; i < layout.type_count; ++i) {
        const VkMemoryType& t = memory_properties_.memoryTypes[i];
        layout.types[i].heap_index = t.heapIndex;
        layout.types[i].device_local = (t.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
        layout.types[i].host_visible = (t.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
        layout.types[i].host_coherent = (t.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        layout.types[i].host_cached = (t.propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0;
    }
    layout.heap_count = memory_properties_.memoryHeapCount;
    for (u32 i = 0; i < layout.heap_count; ++i) {
        layout.heaps[i].size = memory_properties_.memoryHeaps[i].size;
        layout.heaps[i].device_local = (memory_properties_.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
    }
    MemoryAllocatorConfig memory_config;
    memory_config.max_allocation_count = properties_.limits.maxMemoryAllocationCount;
    memory_config.non_coherent_atom_size = properties_.limits.nonCoherentAtomSize;
    memory_backend_ = new VulkanMemoryBackend(&fns_, device_, &device_lost_);
    memory_ = new MemoryAllocator(layout, memory_config, memory_backend_);

    RhiLog(LogVerbosity::Info, "Vulkan: device created on '%s' (queue family %d)", gpu_info_.name, queues_.graphics_family);
    return true;
}

void VulkanDevice::FillCaps() {
    const VkPhysicalDeviceLimits& limits = properties_.limits;
    caps_.max_texture_dimension_2d = limits.maxImageDimension2D;
    caps_.max_texture_dimension_3d = limits.maxImageDimension3D;
    caps_.max_texture_array_layers = limits.maxImageArrayLayers;
    caps_.max_bind_groups = limits.maxBoundDescriptorSets < kMaxBindGroups ? limits.maxBoundDescriptorSets : kMaxBindGroups;
    caps_.max_color_attachments = limits.maxColorAttachments < kMaxColorAttachments ? limits.maxColorAttachments : kMaxColorAttachments;
    caps_.max_push_constant_bytes = limits.maxPushConstantsSize < kMaxPushConstantBytes ? limits.maxPushConstantsSize : kMaxPushConstantBytes;
    caps_.min_uniform_buffer_offset_alignment = static_cast<u32>(limits.minUniformBufferOffsetAlignment);
    caps_.min_storage_buffer_offset_alignment = static_cast<u32>(limits.minStorageBufferOffsetAlignment);
    caps_.max_sampler_anisotropy = anisotropy_enabled_ ? limits.maxSamplerAnisotropy : 1.0f;
    for (int i = 0; i < 3; ++i) {
        caps_.max_compute_work_group_count[i] = limits.maxComputeWorkGroupCount[i];
    }
    // Timestamps need both the device limit and a queue family that can write them.
    const bool timestamps = limits.timestampComputeAndGraphics &&
                            gpu_info_.families[queues_.graphics_family].timestamp_valid_bits > 0;
    caps_.timestamp_period_ns = timestamps ? limits.timestampPeriod : 0.0f;

    // The best depth format the device can attach, in order of preference.
    const Format candidates[] = {Format::D32_Float, Format::D24_UNorm_S8_UInt, Format::D32_Float_S8_UInt};
    bool depth_found = false;
    for (Format candidate : candidates) {
        VkFormatProperties props{};
        instance_.Fns().GetPhysicalDeviceFormatProperties(physical_, ToVkFormat(candidate), &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
            caps_.depth_format = candidate;
            depth_found = true;
            break;
        }
    }
    if (!depth_found) {
        RhiLog(LogVerbosity::Error, "Vulkan: no depth format can be used as an attachment on '%s'; depth targets will fail",
               gpu_info_.name);
    }

    RhiLog(LogVerbosity::Info,
           "Vulkan: caps: tex2d<=%u tex3d<=%u layers<=%u bind_groups=%u color_att=%u push=%uB ubo_align=%u ssbo_align=%u "
           "aniso=%.0f depth=%s ts_period=%.2fns",
           caps_.max_texture_dimension_2d, caps_.max_texture_dimension_3d, caps_.max_texture_array_layers,
           caps_.max_bind_groups, caps_.max_color_attachments, caps_.max_push_constant_bytes,
           caps_.min_uniform_buffer_offset_alignment, caps_.min_storage_buffer_offset_alignment,
           static_cast<double>(caps_.max_sampler_anisotropy), FormatInfo(caps_.depth_format).name,
           static_cast<double>(caps_.timestamp_period_ns));
}

bool VulkanDevice::HeapBudget(u32 heap, u64* budget, u64* usage) const {
    if (!memory_budget_enabled_ || heap >= memory_properties_.memoryHeapCount) {
        return false;
    }
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget_props{};
    budget_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
    VkPhysicalDeviceMemoryProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    props2.pNext = &budget_props;
    instance_.Fns().GetPhysicalDeviceMemoryProperties2(physical_, &props2);
    *budget = budget_props.heapBudget[heap];
    *usage = budget_props.heapUsage[heap];
    return true;
}

VulkanDevice::~VulkanDevice() {
    if (device_ != nullptr) {
        fns_.DeviceWaitIdle(device_);
    }
    delete memory_; // frees every block, so the GPU must be idle first
    delete memory_backend_;
    memory_ = nullptr;
    memory_backend_ = nullptr;
    if (device_ != nullptr) {
        fns_.DestroyDevice(device_, nullptr);
        device_ = nullptr;
    }
    // instance_ is destroyed after the device, as a member.
}

void VulkanDevice::NotImplemented(const char* what) {
    RhiLog(LogVerbosity::Error, "Vulkan: %s is not implemented yet", what);
}

const char* VulkanDevice::BackendName() const { return "vulkan"; }
const DeviceCaps& VulkanDevice::Caps() const { return caps_; }

BufferHandle VulkanDevice::CreateBuffer(const BufferDesc&) { NotImplemented("CreateBuffer"); return {}; }
void VulkanDevice::DestroyBuffer(BufferHandle) {}
TextureHandle VulkanDevice::CreateTexture(const TextureDesc&) { NotImplemented("CreateTexture"); return {}; }
void VulkanDevice::DestroyTexture(TextureHandle) {}
SamplerHandle VulkanDevice::CreateSampler(const SamplerDesc&) { NotImplemented("CreateSampler"); return {}; }
void VulkanDevice::DestroySampler(SamplerHandle) {}
ShaderHandle VulkanDevice::CreateShader(const ShaderDesc&) { NotImplemented("CreateShader"); return {}; }
void VulkanDevice::DestroyShader(ShaderHandle) {}
BindGroupLayoutHandle VulkanDevice::CreateBindGroupLayout(const BindGroupLayoutDesc&) { NotImplemented("CreateBindGroupLayout"); return {}; }
void VulkanDevice::DestroyBindGroupLayout(BindGroupLayoutHandle) {}
BindGroupHandle VulkanDevice::CreateBindGroup(const BindGroupDesc&) { NotImplemented("CreateBindGroup"); return {}; }
void VulkanDevice::DestroyBindGroup(BindGroupHandle) {}
PipelineHandle VulkanDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc&) { NotImplemented("CreateGraphicsPipeline"); return {}; }
PipelineHandle VulkanDevice::CreateComputePipeline(const ComputePipelineDesc&) { NotImplemented("CreateComputePipeline"); return {}; }
void VulkanDevice::DestroyPipeline(PipelineHandle) {}
void* VulkanDevice::MapBuffer(BufferHandle) { NotImplemented("MapBuffer"); return nullptr; }
void VulkanDevice::UnmapBuffer(BufferHandle) {}
CommandList* VulkanDevice::CreateCommandList() { NotImplemented("CreateCommandList"); return nullptr; }
FenceValue VulkanDevice::Submit(CommandList* const*, usize) { NotImplemented("Submit"); return 0; }
bool VulkanDevice::WaitFence(FenceValue value, u64) { return value == 0; } // 0 means "no work": already complete
FenceValue VulkanDevice::CompletedFence() const { return 0; }
void VulkanDevice::BeginFrame() { ++frame_number_; }
void VulkanDevice::EndFrame() {}
u64 VulkanDevice::FrameNumber() const { return frame_number_; }

void VulkanDevice::WaitIdle() {
    VkResult r = fns_.DeviceWaitIdle(device_);
    if (ENGINE_VK_CHECK(r) == VkResultClass::DeviceLost) {
        device_lost_.store(true, std::memory_order_relaxed);
    }
}

bool VulkanDevice::DeviceLost() const { return device_lost_.load(std::memory_order_relaxed); }

} // namespace engine_placeholder::rhi::vulkan
