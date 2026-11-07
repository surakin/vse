#include "rhi/vulkan/vk_loader.h"

#include <cstdlib>
#include <cstring>

#include "os/gpu_driver_probe.h"
#include "rhi/rhi_log.h"

namespace engine_placeholder::rhi::vulkan {

const char* LoaderStatusName(LoaderStatus status) {
    switch (status) {
    case LoaderStatus::Ok: return "Ok";
    case LoaderStatus::NoLoader: return "NoLoader";
    case LoaderStatus::BadLoader: return "BadLoader";
    }
    return "Unknown";
}

LoaderStatus LoadVulkanLoader(VulkanLoader* out) {
    const char* override_path = std::getenv(kVulkanLibraryEnvVar);
    // The platform's loader (libvulkan.so.1, vulkan-1.dll: os::VulkanLoaderName, the one place the name is chosen), found by the OS's own
    // search order (on Windows: never the current directory).
    const char* path = (override_path != nullptr && override_path[0] != '\0') ? override_path : os::VulkanLoaderName();

    os::DynamicLibraryHandle library = os::LoadLibrary(StringView(path, std::strlen(path)));
    if (library == nullptr) {
        RhiLog(LogVerbosity::Warn, "Vulkan: cannot open loader library '%s' (is a Vulkan driver installed?)", path);
        return LoaderStatus::NoLoader;
    }
    void* symbol = os::GetSymbol(library, "vkGetInstanceProcAddr");
    if (symbol == nullptr) {
        // Not a Vulkan loader, so the never-unload rationale (see VulkanLoader) does not apply.
        os::UnloadLibrary(library);
        RhiLog(LogVerbosity::Error, "Vulkan: '%s' does not export vkGetInstanceProcAddr", path);
        return LoaderStatus::BadLoader;
    }
    out->library = library;
    std::memcpy(&out->get_instance_proc_addr, &symbol, sizeof(symbol));
    return LoaderStatus::Ok;
}

} // namespace engine_placeholder::rhi::vulkan
