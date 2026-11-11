#include "rhi/backend_registry.h"
#include "rhi/null/null_device.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"

namespace engine_placeholder::rhi
{

namespace
{

DevicePtr CreateNullBackendDevice(const DeviceCreateInfo& info)
{
    NullDeviceOptions options;
    AdapterList adapters;
    detail::NullAdapters(&adapters);
    if (info.adapter != nullptr && info.adapter[0] != '\0')
    {
        const int match = MatchAdapter(adapters.items, adapters.count, info.adapter);
        if (match < 0)
        {
            RhiLog(LogVerbosity::Error,
                   "CreateDevice: DeviceCreateInfo::adapter='%s' matches none of the %u null adapters", info.adapter,
                   adapters.count);
            return DevicePtr();
        }
        if (!adapters.items[match].suitable)
        {
            RhiLog(LogVerbosity::Error, "CreateDevice: DeviceCreateInfo::adapter='%s' selects '%s', which %s",
                   info.adapter, adapters.items[match].name,
                   adapters.items[match].unsuitable_reason != nullptr ? adapters.items[match].unsuitable_reason
                                                                      : "is unsuitable");
            return DevicePtr();
        }
        options.adapter = adapters.items[match];
    }
    else
    {
        bool found = false;
        for (u32 i = 0; i < adapters.count && !found; ++i)
        {
            if (adapters.items[i].suitable)
            {
                options.adapter = adapters.items[i];
                found = true;
            }
        }
        if (!found)
        {
            RhiLog(LogVerbosity::Error, "CreateDevice: none of the %u null adapters is suitable", adapters.count);
            return DevicePtr();
        }
    }
    options.lose_device_at_frame = info.faults.lose_device_at_frame;
    options.lose_device_at_submit = info.faults.lose_device_at_submit;
    options.lose_device_at_present = info.faults.lose_device_at_present;
    options.strikes_left = info.faults.strikes_left;
    options.fail_surface_recreate = info.faults.fail_surface_recreate;
    options.fail_swapchain_create = info.faults.fail_swapchain_create;
    return CreateNullDevice(options);
}

bool EnumerateNullAdapters(const DeviceCreateInfo&, AdapterList* out)
{
    detail::NullAdapters(out);
    return true;
}

}  // namespace

bool RegisterNullBackend()
{
    BackendDescriptor d;
    d.backend = Backend::Null;
    d.name = "null";
    d.create = &CreateNullBackendDevice;
    d.enumerate_adapters = &EnumerateNullAdapters;
    d.shader_format = ShaderFormat::Spirv;
    d.auto_rank = 0;
    return RegisterBackend(d);
}

}  // namespace engine_placeholder::rhi
