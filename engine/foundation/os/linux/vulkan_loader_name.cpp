#include "os/gpu_driver_probe.h"

namespace vse::os
{

// The versioned name, not "libvulkan.so": the loader package always installs libvulkan.so.1, while the unversioned
// symlink only exists alongside the -dev headers, which are not on every machine with a working Vulkan runtime.
const char* VulkanLoaderName()
{
    return "libvulkan.so.1";
}

}  // namespace vse::os
