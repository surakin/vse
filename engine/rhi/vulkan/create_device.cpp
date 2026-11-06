#include "rhi/rhi.h"

#include "rhi/null/null_device.h"
#include "rhi/rhi_log.h"
#include "rhi/vulkan/vulkan_device.h"

namespace engine_placeholder::rhi {

DevicePtr CreateDevice(const DeviceCreateInfo& info) {
    switch (info.backend) {
    case Backend::Null: {
        NullDeviceOptions options;
        if (info.adapter != nullptr && info.adapter[0] != '\0') {
            AdapterList adapters;
            detail::NullAdapters(&adapters);
            const int match = MatchAdapter(adapters.items, adapters.count, info.adapter);
            if (match < 0) {
                RhiLog(LogVerbosity::Error, "CreateDevice: DeviceCreateInfo::adapter='%s' matches none of the %u null adapters",
                       info.adapter, adapters.count);
                return DevicePtr();
            }
            if (!adapters.items[match].suitable) {
                RhiLog(LogVerbosity::Error, "CreateDevice: DeviceCreateInfo::adapter='%s' selects '%s', which %s", info.adapter,
                       adapters.items[match].name, adapters.items[match].unsuitable_reason != nullptr
                                                       ? adapters.items[match].unsuitable_reason
                                                       : "is unsuitable");
                return DevicePtr();
            }
            options.adapter = adapters.items[match];
        } else {
            AdapterList adapters;
            detail::NullAdapters(&adapters);
            bool found = false;
            for (u32 i = 0; i < adapters.count && !found; ++i) {
                if (adapters.items[i].suitable) {
                    options.adapter = adapters.items[i];
                    found = true;
                }
            }
            if (!found) {
                RhiLog(LogVerbosity::Error, "CreateDevice: none of the %u null adapters is suitable", adapters.count);
                return DevicePtr();
            }
        }
        options.lose_device_at_frame = info.faults.lose_device_at_frame;
        options.fail_surface_recreate = info.faults.fail_surface_recreate;
        return CreateNullDevice(options);
    }
    case Backend::Vulkan: return vulkan::VulkanDevice::Create(info);
    }
    RhiLog(LogVerbosity::Error, "CreateDevice: unknown backend %d", static_cast<int>(info.backend));
    return DevicePtr();
}

bool EnumerateAdapters(const DeviceCreateInfo& info, AdapterList* out) {
    switch (info.backend) {
    case Backend::Null: detail::NullAdapters(out); return true;
    case Backend::Vulkan: return vulkan::VulkanDevice::EnumerateAdapters(info, out);
    }
    *out = AdapterList{};
    return false;
}

} // namespace engine_placeholder::rhi
