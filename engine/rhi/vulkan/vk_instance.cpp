#include "containers/vector.h"
#include "rhi/vulkan/vk_instance.h"

#include "sync/atomic.h"

#include "os/console.h"
#include "cstr/cstr.h"
#include "sync/mutex.h"

#include "log/assert.h"
#include "rhi/rhi_log.h"
#include "rhi/vulkan/vk_result.h"

namespace engine_placeholder::rhi::vulkan {

namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

Atomic<u64> g_error_count{0};
Atomic<u64> g_warning_count{0};
Atomic<bool> g_warned_missing_layer{false};

Mutex g_capture_mutex;
ValidationCaptureFn g_capture_fn = nullptr;
void* g_capture_user = nullptr;

// -1 unset, 0 off, 1 on.
int ParseSwitch(const char* value) {
    if (value == nullptr) {
        return -1;
    }
    if (CStrCompare(value, "1") == 0) {
        return 1;
    }
    if (CStrCompare(value, "0") == 0) {
        return 0;
    }
    return -1;
}

LogVerbosity SeverityToVerbosity(VkDebugUtilsMessageSeverityFlagBitsEXT severity) {
    switch (severity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: return LogVerbosity::Error;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: return LogVerbosity::Warn;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: return LogVerbosity::Debug;
    default: return LogVerbosity::Trace;
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                      VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                                      const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    // `user` carries this instance's ENGINE_RHI_VALIDATION_FATAL setting (1 or null), fixed at creation.
    const bool fatal = user != nullptr;
    LogVerbosity level = SeverityToVerbosity(severity);
    if (level == LogVerbosity::Error) {
        g_error_count.FetchAdd(1, MemoryOrder::Relaxed);
    } else if (level == LogVerbosity::Warn) {
        g_warning_count.FetchAdd(1, MemoryOrder::Relaxed);
    }
    const char* id = (data->pMessageIdName != nullptr) ? data->pMessageIdName : "";
    const char* message = (data->pMessage != nullptr) ? data->pMessage : "";
    RhiLog(level, "validation [%s] %s", id, message);
    if (level == LogVerbosity::Error) {
        // A trace of where the first validation error was reported (an ensure reports once per site); the log has every error.
        ENGINE_ENSURE(false, "a Vulkan validation error was reported");
    }
    // Copy the hook under the lock and call it outside, so a hook may itself log, call Vulkan or replace the hook.
    ValidationCaptureFn capture = nullptr;
    void* capture_user = nullptr;
    {
        LockGuard<Mutex> lock(g_capture_mutex);
        capture = g_capture_fn;
        capture_user = g_capture_user;
    }
    if (capture != nullptr) {
        capture(capture_user, level, id, message);
    }
    if (level == LogVerbosity::Error && fatal) {
        RhiLog(LogVerbosity::Error, "ENGINE_RHI_VALIDATION_FATAL=1: aborting on the first validation error");
        os::Abort();
    }
    return VK_FALSE; // never abort the Vulkan call itself
}

// The properties of one extension list.
struct ExtensionList {
    Vector<VkExtensionProperties> items;
    u32 count = 0;

    bool Load(const VkGlobalFns& fns, const char* layer_name) {
        VkResult r = fns.EnumerateInstanceExtensionProperties(layer_name, &count, nullptr);
        if (r != VK_SUCCESS) {
            return false;
        }
        if (!items.TryResize(count > 0 ? count : 1)) {
            return false;
        }
        r = fns.EnumerateInstanceExtensionProperties(layer_name, &count, items.Data());
        return r == VK_SUCCESS || r == VK_INCOMPLETE;
    }
    bool Has(const char* name) const {
        for (u32 i = 0; i < count; ++i) {
            if (CStrCompare(items[i].extensionName, name) == 0) {
                return true;
            }
        }
        return false;
    }
};

bool HasLayer(const VkGlobalFns& fns, const char* name) {
    u32 count = 0;
    if (fns.EnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }
    Vector<VkLayerProperties> layers;
    if (!layers.TryResize(count)) {
        return false;
    }
    VkResult r = fns.EnumerateInstanceLayerProperties(&count, layers.Data());
    bool found = false;
    if (r == VK_SUCCESS || r == VK_INCOMPLETE) {
        for (u32 i = 0; i < count && !found; ++i) {
            found = CStrCompare(layers[i].layerName, name) == 0;
        }
    }
    return found;
}

} // namespace

ValidationPolicy ResolveValidationPolicy(const VulkanInstanceDesc& desc, const char* env_validation, const char* env_sync,
                                         const char* env_best_practices, bool debug_build) {
    ValidationPolicy policy;
    switch (desc.validation) {
    case ValidationMode::On: policy.enable = true; break;
    case ValidationMode::Off: policy.enable = false; break;
    case ValidationMode::Auto: {
        int env = ParseSwitch(env_validation);
        policy.enable = env >= 0 ? env == 1 : debug_build;
        break;
    }
    }
    if (!policy.enable) {
        return policy;
    }
    int sync = ParseSwitch(env_sync);
    policy.sync = sync >= 0 ? sync == 1 : desc.sync_validation;
    int best = ParseSwitch(env_best_practices);
    policy.best_practices = best >= 0 ? best == 1 : desc.best_practices;
    return policy;
}

ValidationCounts GetValidationCounts() {
    ValidationCounts counts;
    counts.errors = g_error_count.Load(MemoryOrder::Relaxed);
    counts.warnings = g_warning_count.Load(MemoryOrder::Relaxed);
    return counts;
}

void ResetValidationCounts() {
    g_error_count.Store(0, MemoryOrder::Relaxed);
    g_warning_count.Store(0, MemoryOrder::Relaxed);
}

void SetValidationCapture(ValidationCaptureFn fn, void* user) {
    LockGuard<Mutex> lock(g_capture_mutex);
    g_capture_fn = fn;
    g_capture_user = user;
}

VulkanInstance::~VulkanInstance() { Destroy(); }

VulkanInstance::VulkanInstance(VulkanInstance&& other)
    : loader_(other.loader_),
      instance_(other.instance_),
      fns_(other.fns_),
      messenger_(other.messenger_),
      validation_enabled_(other.validation_enabled_),
      presentable_(other.presentable_) {
    other.instance_ = nullptr;
    other.messenger_ = nullptr;
    other.fns_ = VkInstanceFns{};
}

VulkanInstance& VulkanInstance::operator=(VulkanInstance&& other) {
    if (this != &other) {
        Destroy();
        loader_ = other.loader_;
        instance_ = other.instance_;
        fns_ = other.fns_;
        messenger_ = other.messenger_;
        validation_enabled_ = other.validation_enabled_;
        presentable_ = other.presentable_;
        other.instance_ = nullptr;
        other.messenger_ = nullptr;
        other.fns_ = VkInstanceFns{};
    }
    return *this;
}

void VulkanInstance::Destroy() {
    if (instance_ == nullptr) {
        return;
    }
    if (messenger_ != nullptr && fns_.DestroyDebugUtilsMessengerEXT != nullptr) {
        fns_.DestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
    }
    fns_.DestroyInstance(instance_, nullptr);
    instance_ = nullptr;
    messenger_ = nullptr;
    validation_enabled_ = false;
    presentable_ = false;
    fns_ = VkInstanceFns{};
}

VulkanInstance VulkanInstance::Create(const VulkanLoader& loader, const VulkanInstanceDesc& desc) {
    VulkanInstance result;

    VkGlobalFns global;
    if (!LoadGlobalFns(loader, &global) || !CheckInstanceVersion(global, nullptr)) {
        return result;
    }

#ifdef NDEBUG
    constexpr bool kDebugBuild = false;
#else
    constexpr bool kDebugBuild = true;
#endif
    ValidationPolicy policy = ResolveValidationPolicy(desc, os::GetEnvironment("ENGINE_RHI_VALIDATION"),
                                                      os::GetEnvironment("ENGINE_RHI_VALIDATION_SYNC"),
                                                      os::GetEnvironment("ENGINE_RHI_VALIDATION_BEST_PRACTICES"), kDebugBuild);

    ExtensionList instance_exts;
    if (!instance_exts.Load(global, nullptr)) {
        RhiLog(LogVerbosity::Error, "Vulkan: cannot list instance extensions");
        return result;
    }

    // Validation needs the layer and VK_EXT_debug_utils; without either, say so once and carry on without it.
    bool use_validation = policy.enable;
    if (use_validation && !HasLayer(global, kValidationLayer)) {
        if (!g_warned_missing_layer.Exchange(true)) {
            RhiLog(LogVerbosity::Warn, "Vulkan: validation requested but %s is not installed; continuing without it",
                   kValidationLayer);
        }
        use_validation = false;
    }
    if (use_validation && !instance_exts.Has(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        RhiLog(LogVerbosity::Warn, "Vulkan: %s is unavailable; continuing without validation",
               VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        use_validation = false;
    }

    const char* extensions[8];
    u32 extension_count = 0;
    if (use_validation) {
        extensions[extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }
    if (desc.presentable) {
        if (!instance_exts.Has(VK_KHR_SURFACE_EXTENSION_NAME)) {
            RhiLog(LogVerbosity::Error, "Vulkan: required extension %s is missing (cannot present)",
                   VK_KHR_SURFACE_EXTENSION_NAME);
            return result;
        }
        extensions[extension_count++] = VK_KHR_SURFACE_EXTENSION_NAME;
        bool any_platform = false;
        if (instance_exts.Has("VK_KHR_wayland_surface")) {
            extensions[extension_count++] = "VK_KHR_wayland_surface";
            any_platform = true;
        }
        if (instance_exts.Has("VK_KHR_xlib_surface")) {
            extensions[extension_count++] = "VK_KHR_xlib_surface";
            any_platform = true;
        }
        if (instance_exts.Has("VK_KHR_win32_surface")) {
            extensions[extension_count++] = "VK_KHR_win32_surface";
            any_platform = true;
        }
        if (instance_exts.Has("VK_EXT_swapchain_colorspace")) {
            extensions[extension_count++] = "VK_EXT_swapchain_colorspace"; // lets surfaces offer HDR color spaces
        }
        if (!any_platform) {
            RhiLog(LogVerbosity::Error, "Vulkan: no platform surface extension (VK_KHR_wayland_surface, VK_KHR_xlib_surface, VK_KHR_win32_surface) is available");
            return result;
        }
    }

    // Synchronization and best-practices checks are layer features, enabled through VK_EXT_validation_features.
    VkValidationFeatureEnableEXT enables[2];
    u32 enable_count = 0;
    if (use_validation && (policy.sync || policy.best_practices)) {
        ExtensionList layer_exts;
        if (layer_exts.Load(global, kValidationLayer) && layer_exts.Has(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME)) {
            extensions[extension_count++] = VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME;
            if (policy.sync) {
                enables[enable_count++] = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
            }
            if (policy.best_practices) {
                enables[enable_count++] = VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT;
            }
        } else {
            RhiLog(LogVerbosity::Warn, "Vulkan: %s is unavailable; sync/best-practices validation is off",
                   VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        }
    }
    VkValidationFeaturesEXT features{};
    features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
    features.enabledValidationFeatureCount = enable_count;
    features.pEnabledValidationFeatures = enables;

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = desc.application_name;
    app.applicationVersion = desc.application_version;
    app.pEngineName = "engine_placeholder";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_3;

    // The messenger create-info is chained into vkCreateInstance too, so messages raised while the instance is
    // being created or destroyed are counted and logged; the persistent messenger is created right after.
    const char* fatal_env = os::GetEnvironment("ENGINE_RHI_VALIDATION_FATAL");
    const bool fatal = fatal_env != nullptr && CStrCompare(fatal_env, "1") == 0;
    VkDebugUtilsMessengerCreateInfoEXT mci{};
    mci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    mci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    if (LogRhi.ShouldLog(LogVerbosity::Debug)) {
        mci.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
    }
    if (LogRhi.ShouldLog(LogVerbosity::Trace)) {
        mci.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
    }
    mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    mci.pfnUserCallback = &DebugMessengerCallback;
    mci.pUserData = fatal ? reinterpret_cast<void*>(static_cast<uintptr_t>(1)) : nullptr;
    features.pNext = use_validation ? &mci : nullptr;

    const char* layers[1] = {kValidationLayer};
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pNext = (enable_count > 0) ? static_cast<const void*>(&features) : (use_validation ? static_cast<const void*>(&mci) : nullptr);
    info.pApplicationInfo = &app;
    info.enabledLayerCount = use_validation ? 1 : 0;
    info.ppEnabledLayerNames = layers;
    info.enabledExtensionCount = extension_count;
    info.ppEnabledExtensionNames = extensions;

    VkInstance instance = nullptr;
    if (ENGINE_VK_CHECK(global.CreateInstance(&info, nullptr, &instance)) != VkResultClass::Success) {
        return result;
    }
    VkInstanceFns fns;
    if (!LoadInstanceFns(loader, instance, &fns)) {
        // Cannot destroy without DestroyInstance; fetch it directly.
        PFN_vkDestroyInstance destroy = reinterpret_cast<PFN_vkDestroyInstance>(
            loader.get_instance_proc_addr(instance, "vkDestroyInstance"));
        if (destroy != nullptr) {
            destroy(instance, nullptr);
        }
        return result;
    }

    result.loader_ = loader;
    result.instance_ = instance;
    result.fns_ = fns;
    result.presentable_ = desc.presentable;
    result.validation_enabled_ = use_validation;

    if (use_validation) {
        // Without a messenger nothing would be counted and every test would pass vacuously, so this is a failure.
        if (ENGINE_VK_CHECK(fns.CreateDebugUtilsMessengerEXT(instance, &mci, nullptr, &result.messenger_)) !=
            VkResultClass::Success) {
            RhiLog(LogVerbosity::Error, "Vulkan: debug messenger creation failed; refusing a validation instance whose output is lost");
            result.messenger_ = nullptr;
            result.Destroy();
        }
    }
    return result;
}

} // namespace engine_placeholder::rhi::vulkan
