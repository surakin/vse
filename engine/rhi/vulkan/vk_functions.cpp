#include "rhi/vulkan/vk_functions.h"

#include <cstddef>
#include <cstring>

#include "rhi/rhi_log.h"

namespace engine_placeholder::rhi::vulkan {

namespace {

// One row per table member, generated from vk_functions.inc: the Vulkan name, where the pointer lives in
// its table, and whether load fails without it.
struct FnEntry {
    const char* name;
    usize offset;
    bool mandatory;
};

constexpr FnEntry kGlobalEntries[] = {
#define VK_GLOBAL_FN(n) {"vk" #n, offsetof(VkGlobalFns, n), true},
#define VK_GLOBAL_FN_OPT(n) {"vk" #n, offsetof(VkGlobalFns, n), false},
#include "rhi/vulkan/vk_functions.inc"
};

constexpr FnEntry kInstanceEntries[] = {
#define VK_INSTANCE_FN(n) {"vk" #n, offsetof(VkInstanceFns, n), true},
#define VK_INSTANCE_FN_OPT(n) {"vk" #n, offsetof(VkInstanceFns, n), false},
#include "rhi/vulkan/vk_functions.inc"
};

constexpr FnEntry kDeviceEntries[] = {
#define VK_DEVICE_FN(n) {"vk" #n, offsetof(VkDeviceFns, n), true},
#define VK_DEVICE_FN_OPT(n) {"vk" #n, offsetof(VkDeviceFns, n), false},
#include "rhi/vulkan/vk_functions.inc"
};

template <usize N, typename Resolve>
bool LoadTable(const FnEntry (&entries)[N], void* table, usize table_size, const char* scope, Resolve resolve) {
    bool ok = true;
    for (const FnEntry& entry : entries) {
        PFN_vkVoidFunction fn = resolve(entry.name);
        if (fn == nullptr && entry.mandatory) {
            RhiLog(LogVerbosity::Error, "Vulkan: mandatory %s function %s is missing", scope, entry.name);
            ok = false;
            continue; // keep scanning so every missing mandatory function is logged in one pass
        }
        std::memcpy(static_cast<char*>(table) + entry.offset, &fn, sizeof(fn));
    }
    if (!ok) {
        std::memset(table, 0, table_size); // a half-loaded table is never handed out: callers get all-null
    }
    return ok;
}

} // namespace

bool LoadGlobalFns(const VulkanLoader& loader, VkGlobalFns* out) {
    *out = VkGlobalFns{};
    return LoadTable(kGlobalEntries, out, sizeof(*out), "global",
                     [&](const char* name) { return loader.get_instance_proc_addr(nullptr, name); });
}

bool LoadInstanceFns(const VulkanLoader& loader, VkInstance instance, VkInstanceFns* out) {
    *out = VkInstanceFns{};
    return LoadTable(kInstanceEntries, out, sizeof(*out), "instance",
                     [&](const char* name) { return loader.get_instance_proc_addr(instance, name); });
}

bool LoadDeviceFns(const VkInstanceFns& instance_fns, VkDevice device, VkDeviceFns* out) {
    *out = VkDeviceFns{};
    return LoadTable(kDeviceEntries, out, sizeof(*out), "device",
                     [&](const char* name) { return instance_fns.GetDeviceProcAddr(device, name); });
}

bool CheckInstanceVersion(const VkGlobalFns& fns, u32* out_version) {
    u32 version = VK_API_VERSION_1_0;
    if (fns.EnumerateInstanceVersion != nullptr) {
        VkResult result = fns.EnumerateInstanceVersion(&version);
        if (result != VK_SUCCESS) {
            RhiLog(LogVerbosity::Error, "Vulkan: vkEnumerateInstanceVersion failed (%d)", static_cast<int>(result));
            if (out_version != nullptr) {
                *out_version = VK_API_VERSION_1_0;
            }
            return false;
        }
    }
    if (out_version != nullptr) {
        *out_version = version;
    }
    RhiLog(LogVerbosity::Info, "Vulkan: instance version %u.%u.%u", VK_API_VERSION_MAJOR(version),
           VK_API_VERSION_MINOR(version), VK_API_VERSION_PATCH(version));
    if (version < kRequiredVulkanVersion) {
        RhiLog(LogVerbosity::Error, "Vulkan: driver too old (found %u.%u, need 1.3); update the graphics driver",
               VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version));
        return false;
    }
    return true;
}

void LogInstanceCapabilities(const VkGlobalFns& fns) {
    // Fixed-size, heap-free; the two lists are scoped so at most ~17 KB of stack is live at once.
    constexpr u32 kMaxExtensions = 64;
    constexpr u32 kMaxLayers = 32;
    if (!LogRhi.ShouldLog(LogVerbosity::Debug)) {
        return;
    }
    {
        VkExtensionProperties props[kMaxExtensions];
        u32 count = kMaxExtensions;
        VkResult result = fns.EnumerateInstanceExtensionProperties(nullptr, &count, props);
        if (result == VK_SUCCESS || result == VK_INCOMPLETE) {
            for (u32 i = 0; i < count; ++i) {
                RhiLog(LogVerbosity::Debug, "Vulkan: instance extension %s (rev %u)", props[i].extensionName,
                       props[i].specVersion);
            }
            if (result == VK_INCOMPLETE) {
                RhiLog(LogVerbosity::Debug, "Vulkan: instance extension list truncated at %u", kMaxExtensions);
            }
        } else {
            RhiLog(LogVerbosity::Warn, "Vulkan: cannot list instance extensions (%d)", static_cast<int>(result));
        }
    }
    {
        VkLayerProperties props[kMaxLayers];
        u32 count = kMaxLayers;
        VkResult result = fns.EnumerateInstanceLayerProperties(&count, props);
        if (result == VK_SUCCESS || result == VK_INCOMPLETE) {
            for (u32 i = 0; i < count; ++i) {
                RhiLog(LogVerbosity::Debug, "Vulkan: instance layer %s", props[i].layerName);
            }
            if (result == VK_INCOMPLETE) {
                RhiLog(LogVerbosity::Debug, "Vulkan: instance layer list truncated at %u", kMaxLayers);
            }
        } else {
            RhiLog(LogVerbosity::Warn, "Vulkan: cannot list instance layers (%d)", static_cast<int>(result));
        }
    }
}

} // namespace engine_placeholder::rhi::vulkan
