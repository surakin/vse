#include "os/gpu_driver_probe.h"

#include "log/log.h"
#include "os/os.h"

#include "cstr/format.h"

namespace vse::os
{

PFN_vkVoidFunction ProbeVulkanCreateInstance()
{
    char line[96];
    DynamicLibraryHandle vulkan = LoadLibrary(StringView(VulkanLoaderName()));
    if (vulkan == nullptr)
    {
        FormatTo(line, sizeof(line), "GPU driver probe: failed to load %s", VulkanLoaderName());
        Log(LogPlatform, LogVerbosity::Error, StringView(line));
        return nullptr;
    }
    FormatTo(line, sizeof(line), "GPU driver probe: loaded %s", VulkanLoaderName());
    Log(LogPlatform, LogVerbosity::Info, StringView(line));

    void* symbol = GetSymbol(vulkan, "vkGetInstanceProcAddr");
    if (symbol == nullptr)
    {
        Log(LogPlatform, LogVerbosity::Error, "GPU driver probe: vkGetInstanceProcAddr not found");
        return nullptr;
    }
    auto get_instance_proc_addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(symbol);
    Log(LogPlatform, LogVerbosity::Info, "GPU driver probe: resolved vkGetInstanceProcAddr");

    // Passing VK_NULL_HANDLE resolves global-level entry points
    // (vkCreateInstance, vkEnumerateInstance*) per the Vulkan spec.
    PFN_vkVoidFunction create_instance = get_instance_proc_addr(nullptr, "vkCreateInstance");
    if (create_instance == nullptr)
    {
        Log(LogPlatform, LogVerbosity::Error, "GPU driver probe: vkCreateInstance not found");
        return nullptr;
    }
    Log(LogPlatform, LogVerbosity::Info, "GPU driver probe: resolved vkCreateInstance");

    return create_instance;
}

}  // namespace vse::os
