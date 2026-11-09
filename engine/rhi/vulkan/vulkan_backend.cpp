#include "rhi/vulkan/vulkan_backend.h"

#include "cstr/format.h"

#include "rhi/backend_registry.h"
#include "rhi/rhi.h"
#include "rhi/vulkan/vk_loader.h"
#include "rhi/vulkan/vulkan_device.h"

namespace engine_placeholder::rhi {

namespace {

DevicePtr CreateVulkanBackendDevice(const DeviceCreateInfo& info) { return vulkan::VulkanDevice::Create(info); }

bool EnumerateVulkanAdapters(const DeviceCreateInfo& info, AdapterList* out) { return vulkan::VulkanDevice::EnumerateAdapters(info, out); }

bool VulkanAvailable(char* reason, usize reason_size) {
    vulkan::VulkanLoader loader;
    const vulkan::LoaderStatus status = vulkan::LoadVulkanLoader(&loader);
    if (status == vulkan::LoaderStatus::Ok) {
        return true;
    }
    FormatTo(reason, reason_size, "the Vulkan loader could not be used (%s)", vulkan::LoaderStatusName(status));
    return false;
}

} // namespace

bool RegisterVulkanBackend() {
    BackendDescriptor d;
    d.backend = Backend::Vulkan;
    d.name = "vulkan";
    d.create = &CreateVulkanBackendDevice;
    d.enumerate_adapters = &EnumerateVulkanAdapters;
    d.available = &VulkanAvailable;
    d.shader_format = ShaderFormat::Spirv;
    d.auto_rank = 10;
    return RegisterBackend(d);
}

} // namespace engine_placeholder::rhi
