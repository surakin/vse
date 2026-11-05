#include "rhi/rhi.h"

#include "rhi/null/null_device.h"
#include "rhi/rhi_log.h"
#include "rhi/vulkan/vulkan_device.h"

namespace engine_placeholder::rhi {

DevicePtr CreateDevice(const DeviceCreateInfo& info) {
    switch (info.backend) {
    case Backend::Null: {
        NullDeviceOptions options;
        options.lose_device_at_frame = info.faults.lose_device_at_frame;
        return CreateNullDevice(options);
    }
    case Backend::Vulkan: return vulkan::VulkanDevice::Create(info);
    }
    RhiLog(LogVerbosity::Error, "CreateDevice: unknown backend %d", static_cast<int>(info.backend));
    return DevicePtr();
}

} // namespace engine_placeholder::rhi
