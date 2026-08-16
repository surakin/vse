#include "os/gpu_driver_probe.h"

#include "log/log.h"
#include "os/os.h"

namespace engine_placeholder::os {

PFN_vkVoidFunction ProbeVulkanCreateInstance() {
    // Versioned name deliberately, not "libvulkan.so": the loader package
    // always installs libvulkan.so.1; the unversioned symlink only exists
    // alongside the -dev headers, which aren't guaranteed on every
    // machine that has a working Vulkan runtime.
    DynamicLibraryHandle vulkan = LoadLibrary("libvulkan.so.1");
    if (vulkan == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "GPU driver probe: failed to load libvulkan.so.1");
        return nullptr;
    }
    Log(LogPlatform, LogVerbosity::Info, "GPU driver probe: loaded libvulkan.so.1");

    void* symbol = GetSymbol(vulkan, "vkGetInstanceProcAddr");
    if (symbol == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "GPU driver probe: vkGetInstanceProcAddr not found");
        return nullptr;
    }
    auto get_instance_proc_addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(symbol);
    Log(LogPlatform, LogVerbosity::Info, "GPU driver probe: resolved vkGetInstanceProcAddr");

    // Passing VK_NULL_HANDLE resolves global-level entry points
    // (vkCreateInstance, vkEnumerateInstance*) per the Vulkan spec.
    PFN_vkVoidFunction create_instance = get_instance_proc_addr(nullptr, "vkCreateInstance");
    if (create_instance == nullptr) {
        Log(LogPlatform, LogVerbosity::Error, "GPU driver probe: vkCreateInstance not found");
        return nullptr;
    }
    Log(LogPlatform, LogVerbosity::Info, "GPU driver probe: resolved vkCreateInstance");

    return create_instance;
}

} // namespace engine_placeholder::os
