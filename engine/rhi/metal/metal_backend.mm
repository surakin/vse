// The Metal backend's registration (Phase 21 Day 300; metal_backend.h).
#include "rhi/metal/metal_backend.h"

#include "cstr/format.h"

#include "rhi/backend_registry.h"
#include "rhi/metal/metal_adapters.h"
#include "rhi/metal/metal_device.h"
#include "rhi/rhi.h"

#import <Foundation/Foundation.h>

namespace vse::rhi
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
    d.auto_rank = 30;  // the GPU backend of macOS: Auto means Metal there (Phase 21 Day 309; by name only from Day 300)
    return RegisterBackend(d);
}

}  // namespace vse::rhi
