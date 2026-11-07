#include "os/gpu_driver_probe.h"

namespace engine_placeholder::os {

// Installed by every Vulkan driver into System32 (the Vulkan SDK is not needed to run).
const char* VulkanLoaderName() {
    return "vulkan-1.dll";
}

} // namespace engine_placeholder::os
