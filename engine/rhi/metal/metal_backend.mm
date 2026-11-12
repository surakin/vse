// The Metal backend's registration (Phase 26 Day 316; metal_backend.h).
#include "rhi/metal/metal_backend.h"

#include "cstr/format.h"

#include "rhi/backend_registry.h"
#include "rhi/metal/metal_adapters.h"
#include "rhi/metal/metal_device.h"
#include "rhi/rhi.h"

#import <Foundation/Foundation.h>

namespace engine_placeholder::rhi
{

namespace
{

DevicePtr CreateMetalBackendDevice(const DeviceCreateInfo& info)
{
    return metal::MetalDevice::Create(info);
}

bool EnumerateMetalAdapters(const DeviceCreateInfo& info, AdapterList* out)
{
    return metal::MetalDevice::EnumerateAdapters(info, out);
}

bool MetalAvailable(char* reason, usize reason_size)
{
    @autoreleasepool
    {
        metal::MetalAdapterSet set;
        if (!metal::ListMetalAdapters(&set))
        {
            FormatTo(reason, reason_size, "this Mac has no Metal device");
            return false;
        }
        for (u32 i = 0; i < set.list.count; ++i)
        {
            if (set.list.items[i].suitable)
            {
                return true;
            }
        }
        FormatTo(reason, reason_size, "no Metal device of the Mac2 family supports Metal 3");
        return false;
    }
}

}  // namespace

bool RegisterMetalBackend()
{
    BackendDescriptor d;
    d.backend = Backend::Metal;
    d.name = "metal";
    d.create = &CreateMetalBackendDevice;
    d.enumerate_adapters = &EnumerateMetalAdapters;
    d.available = &MetalAvailable;
    d.shader_format = ShaderFormat::MetalLib;
    d.auto_rank = 0;  // by name only until the suites pass on it (Day 325, which makes Auto mean Metal on macOS)
    return RegisterBackend(d);
}

}  // namespace engine_placeholder::rhi
