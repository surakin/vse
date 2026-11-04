#include "rhi/rhi.h"

#include "rhi/null/null_device.h"
#include "rhi/rhi_log.h"
#include "rhi/vulkan/vulkan_device.h"

namespace engine_placeholder::rhi {

Device* CreateDevice(const DeviceCreateInfo& info) {
    switch (info.backend) {
    case Backend::Null: return CreateNullDevice();
    case Backend::Vulkan: return vulkan::VulkanDevice::Create(info);
    }
    RhiLog(LogVerbosity::Error, "CreateDevice: unknown backend %d", static_cast<int>(info.backend));
    return nullptr;
}

} // namespace engine_placeholder::rhi
