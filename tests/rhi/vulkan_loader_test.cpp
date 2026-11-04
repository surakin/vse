#include <cstdlib>
#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gpu_fixture.h"
#include "rhi/vulkan/vk_functions.h"
#include "rhi/vulkan/vk_loader.h"
#include "rhi/vulkan/vk_result.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi::vulkan;

// A real Vulkan driver (and the libraries it pulls in, e.g. libdbus via the NVIDIA ICD) leaves process-lifetime
// allocations that LeakSanitizer would report against this test binary. They are not ours, so suppress those
// libraries only; leaks in engine code are still reported. The last entry is the one messenger that
// vulkan_instance_test.cpp leaks on purpose to prove destroy-time validation errors are counted.
extern "C" const char* __lsan_default_suppressions() {
    return "leak:libdbus-1.so\nleak:libnvidia\nleak:libGLX\nleak:libX11\nleak:libxcb\nleak:terminator_CreateDebugUtilsMessengerEXT\n";
}

namespace {

using gpu_test::GpuRequired;

// Skips (or, with ENGINE_RHI_REQUIRE_GPU=1, fails) when this machine has no Vulkan loader.
#define REQUIRE_LOADER(loader)                                                \
    VulkanLoader loader;                                                      \
    if (LoadVulkanLoader(&loader) != LoaderStatus::Ok) {                      \
        if (GpuRequired()) {                                                  \
            FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no Vulkan loader was found");  \
        }                                                                     \
        SKIP("no Vulkan loader on this machine");                             \
    }

struct EnvGuard {
    explicit EnvGuard(const char* value) { setenv(kVulkanLibraryEnvVar, value, 1); }
    ~EnvGuard() { unsetenv(kVulkanLibraryEnvVar); }
};

// Creates a bare Vulkan 1.3 instance (no extensions) for the GPU tests.
VkInstance MakeInstance(const VkGlobalFns& global) {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    VkInstance instance = nullptr;
    REQUIRE(ENGINE_VK_CHECK(global.CreateInstance(&info, nullptr, &instance)) == VkResultClass::Success);
    return instance;
}

} // namespace

TEST_CASE("Vulkan loader reports NoLoader for a missing library without crashing", "[rhi][vulkan]") {
    EnvGuard guard("/nonexistent/libvulkan.so.1");
    VulkanLoader loader;
    CHECK(LoadVulkanLoader(&loader) == LoaderStatus::NoLoader);
    CHECK(loader.library == nullptr);
    CHECK(loader.get_instance_proc_addr == nullptr);
}

TEST_CASE("Vulkan loader reports BadLoader for a library without vkGetInstanceProcAddr", "[rhi][vulkan]") {
    EnvGuard guard("libm.so.6");
    VulkanLoader loader;
    CHECK(LoadVulkanLoader(&loader) == LoaderStatus::BadLoader);
    CHECK(loader.get_instance_proc_addr == nullptr);
}

TEST_CASE("LoaderStatusName names every status", "[rhi][vulkan]") {
    CHECK(std::strcmp(LoaderStatusName(LoaderStatus::Ok), "Ok") == 0);
    CHECK(std::strcmp(LoaderStatusName(LoaderStatus::NoLoader), "NoLoader") == 0);
    CHECK(std::strcmp(LoaderStatusName(LoaderStatus::BadLoader), "BadLoader") == 0);
}

TEST_CASE("Vulkan loader resolves the global and instance tables", "[rhi][vulkan][gpu]") {
    REQUIRE_LOADER(loader);
    CHECK(loader.get_instance_proc_addr != nullptr);

    VkGlobalFns global;
    REQUIRE(LoadGlobalFns(loader, &global));
    CHECK(global.CreateInstance != nullptr);
    CHECK(global.EnumerateInstanceExtensionProperties != nullptr);
    CHECK(global.EnumerateInstanceLayerProperties != nullptr);

    u32 version = 0;
    if (!CheckInstanceVersion(global, &version)) {
        if (GpuRequired()) {
            FAIL("ENGINE_RHI_REQUIRE_GPU=1 but the Vulkan loader is older than 1.3");
        }
        SKIP("Vulkan loader older than 1.3");
    }
    CHECK(version >= VK_API_VERSION_1_3);
    LogInstanceCapabilities(global);

    VkInstance instance = MakeInstance(global);
    VkInstanceFns fns;
    CHECK(LoadInstanceFns(loader, instance, &fns));
    CHECK(fns.EnumeratePhysicalDevices != nullptr);
    CHECK(fns.GetDeviceProcAddr != nullptr);

    u32 device_count = 0;
    CHECK(ENGINE_VK_CHECK(fns.EnumeratePhysicalDevices(instance, &device_count, nullptr)) == VkResultClass::Success);
    fns.DestroyInstance(instance, nullptr);
}

TEST_CASE("Vulkan device table loads every mandatory function on a real device", "[rhi][vulkan][gpu]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    VkInstance instance = fx.Instance().Handle();
    const VkInstanceFns& fns = fx.Instance().Fns();

    u32 count = 1;
    VkPhysicalDevice physical = nullptr;
    VkResult enumerated = fns.EnumeratePhysicalDevices(instance, &count, &physical);
    if (physical == nullptr || (enumerated != VK_SUCCESS && enumerated != VK_INCOMPLETE)) {
        if (GpuRequired()) {
            FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no Vulkan physical device was found");
        }
        SKIP("no Vulkan physical device");
    }

    VkPhysicalDeviceProperties props{};
    fns.GetPhysicalDeviceProperties(physical, &props);
    if (props.apiVersion < VK_API_VERSION_1_3) {
        if (GpuRequired()) {
            FAIL("ENGINE_RHI_REQUIRE_GPU=1 but the first Vulkan device is older than 1.3");
        }
        SKIP("first Vulkan device is older than 1.3");
    }
    // Find a queue family with graphics+compute rather than assuming family 0.
    u32 family_count = 0;
    fns.GetPhysicalDeviceQueueFamilyProperties(physical, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    fns.GetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families.data());
    u32 family = family_count;
    for (u32 i = 0; i < family_count; ++i) {
        if ((families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
            (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
            family = i;
            break;
        }
    }
    REQUIRE(family < family_count);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{};
    queue.queueFamilyIndex = family;
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features f12{};
    f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    f12.pNext = &f13;
    f12.timelineSemaphore = VK_TRUE;
    VkDeviceCreateInfo dinfo{};
    dinfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dinfo.pNext = &f12;
    dinfo.queueCreateInfoCount = 1;
    dinfo.pQueueCreateInfos = &queue;
    VkDevice device = nullptr;
    // A 1.3 device must be able to create this device; a failure is a real failure, not a skip.
    REQUIRE(ENGINE_VK_CHECK(fns.CreateDevice(physical, &dinfo, nullptr, &device)) == VkResultClass::Success);

    VkDeviceFns dev;
    CHECK(LoadDeviceFns(fns, device, &dev));
    CHECK(dev.QueueSubmit2 != nullptr);
    CHECK(dev.CmdBeginRendering != nullptr);
    CHECK(dev.CmdPipelineBarrier2 != nullptr);
    CHECK(dev.WaitSemaphores != nullptr);
    dev.DestroyDevice(device, nullptr);
}

namespace {

// A fake vkGetInstanceProcAddr: resolves every name to a dummy function except the ones listed as missing,
// so the mandatory/optional contract can be tested without a driver.
const char* g_missing[4];
int g_missing_count = 0;
void DummyFn() {}
PFN_vkVoidFunction VKAPI_PTR FakeGetInstanceProcAddr(VkInstance, const char* name) {
    for (int i = 0; i < g_missing_count; ++i) {
        if (std::strcmp(name, g_missing[i]) == 0) {
            return nullptr;
        }
    }
    return &DummyFn;
}
VulkanLoader FakeLoader(std::initializer_list<const char*> missing) {
    g_missing_count = 0;
    for (const char* name : missing) {
        g_missing[g_missing_count++] = name;
    }
    VulkanLoader loader;
    loader.get_instance_proc_addr = &FakeGetInstanceProcAddr;
    return loader;
}

VkResult FakeVersion12(u32* out) {
    *out = VK_API_VERSION_1_2;
    return VK_SUCCESS;
}
VkResult FakeVersion13(u32* out) {
    *out = VK_MAKE_API_VERSION(0, 1, 3, 280);
    return VK_SUCCESS;
}
VkResult FakeVersionFails(u32*) { return VK_ERROR_OUT_OF_HOST_MEMORY; }
} // namespace

TEST_CASE("CheckInstanceVersion rejects a Vulkan 1.0 loader", "[rhi][vulkan]") {
    VkGlobalFns fns; // EnumerateInstanceVersion left null, as on a 1.0 loader
    u32 version = 0;
    CHECK_FALSE(CheckInstanceVersion(fns, &version));
    CHECK(version == VK_API_VERSION_1_0);
}

TEST_CASE("CheckInstanceVersion requires 1.3", "[rhi][vulkan]") {
    VkGlobalFns fns;
    u32 version = 0;
    fns.EnumerateInstanceVersion = &FakeVersion12;
    CHECK_FALSE(CheckInstanceVersion(fns, &version));
    CHECK(version == VK_API_VERSION_1_2);
    fns.EnumerateInstanceVersion = &FakeVersion13;
    CHECK(CheckInstanceVersion(fns, nullptr));
    fns.EnumerateInstanceVersion = &FakeVersionFails;
    CHECK_FALSE(CheckInstanceVersion(fns, &version));
}

TEST_CASE("VkResultName covers the results the backend uses", "[rhi][vulkan]") {
    CHECK(std::strcmp(VkResultName(VK_SUCCESS), "VK_SUCCESS") == 0);
    CHECK(std::strcmp(VkResultName(VK_TIMEOUT), "VK_TIMEOUT") == 0);
    CHECK(std::strcmp(VkResultName(VK_NOT_READY), "VK_NOT_READY") == 0);
    CHECK(std::strcmp(VkResultName(VK_INCOMPLETE), "VK_INCOMPLETE") == 0);
    CHECK(std::strcmp(VkResultName(VK_SUBOPTIMAL_KHR), "VK_SUBOPTIMAL_KHR") == 0);
    CHECK(std::strcmp(VkResultName(VK_ERROR_OUT_OF_DATE_KHR), "VK_ERROR_OUT_OF_DATE_KHR") == 0);
    CHECK(std::strcmp(VkResultName(VK_ERROR_DEVICE_LOST), "VK_ERROR_DEVICE_LOST") == 0);
    CHECK(std::strcmp(VkResultName(VK_ERROR_SURFACE_LOST_KHR), "VK_ERROR_SURFACE_LOST_KHR") == 0);
    CHECK(std::strcmp(VkResultName(VK_ERROR_OUT_OF_DEVICE_MEMORY), "VK_ERROR_OUT_OF_DEVICE_MEMORY") == 0);
    CHECK(std::strcmp(VkResultName(VK_ERROR_OUT_OF_HOST_MEMORY), "VK_ERROR_OUT_OF_HOST_MEMORY") == 0);
    CHECK(std::strcmp(VkResultName(VK_ERROR_INITIALIZATION_FAILED), "VK_ERROR_INITIALIZATION_FAILED") == 0);
    CHECK(std::strcmp(VkResultName(VK_ERROR_FEATURE_NOT_PRESENT), "VK_ERROR_FEATURE_NOT_PRESENT") == 0);
    CHECK(std::strcmp(VkResultName(static_cast<VkResult>(-424242)), "VK_RESULT_UNKNOWN") == 0);
}

TEST_CASE("ClassifyVkResult separates success, recoverable, device-lost and fatal", "[rhi][vulkan]") {
    CHECK(ClassifyVkResult(VK_SUCCESS) == VkResultClass::Success);
    CHECK(ClassifyVkResult(VK_NOT_READY) == VkResultClass::Success);
    CHECK(ClassifyVkResult(VK_TIMEOUT) == VkResultClass::Success);
    CHECK(ClassifyVkResult(VK_INCOMPLETE) == VkResultClass::Success);
    CHECK(ClassifyVkResult(VK_SUBOPTIMAL_KHR) == VkResultClass::Suboptimal);
    CHECK(ClassifyVkResult(VK_ERROR_OUT_OF_DATE_KHR) == VkResultClass::Recoverable);
    CHECK(ClassifyVkResult(VK_ERROR_DEVICE_LOST) == VkResultClass::DeviceLost);
    CHECK(ClassifyVkResult(VK_ERROR_OUT_OF_HOST_MEMORY) == VkResultClass::Fatal);
    CHECK(ClassifyVkResult(VK_ERROR_SURFACE_LOST_KHR) == VkResultClass::Recoverable);
    CHECK(ClassifyVkResult(static_cast<VkResult>(-424242)) == VkResultClass::Fatal);
}

TEST_CASE("Function tables: optional entries may be null, mandatory ones may not", "[rhi][vulkan]") {
    SECTION("everything resolves") {
        VulkanLoader loader = FakeLoader({});
        VkGlobalFns global;
        VkInstanceFns instance;
        REQUIRE(LoadGlobalFns(loader, &global));
        REQUIRE(LoadInstanceFns(loader, nullptr, &instance));
        CHECK(global.CreateInstance != nullptr);
        CHECK(instance.CreateDebugUtilsMessengerEXT != nullptr);
    }
    SECTION("a missing optional entry loads and is null") {
        VulkanLoader loader = FakeLoader({"vkCreateDebugUtilsMessengerEXT", "vkEnumerateInstanceVersion"});
        VkGlobalFns global;
        VkInstanceFns instance;
        CHECK(LoadGlobalFns(loader, &global));
        CHECK(LoadInstanceFns(loader, nullptr, &instance));
        CHECK(global.EnumerateInstanceVersion == nullptr);
        CHECK(instance.CreateDebugUtilsMessengerEXT == nullptr);
        CHECK(instance.EnumeratePhysicalDevices != nullptr);
    }
    SECTION("a missing mandatory entry fails and leaves the table all-null") {
        VulkanLoader loader = FakeLoader({"vkCreateInstance"});
        VkGlobalFns global;
        global.EnumerateInstanceLayerProperties = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(&DummyFn);
        CHECK_FALSE(LoadGlobalFns(loader, &global));
        CHECK(global.CreateInstance == nullptr);
        CHECK(global.EnumerateInstanceLayerProperties == nullptr); // the resolved ones are cleared too
    }
    SECTION("device tables go through vkGetDeviceProcAddr") {
        VkInstanceFns instance;
        instance.GetDeviceProcAddr = [](VkDevice, const char* name) -> PFN_vkVoidFunction {
            return std::strcmp(name, "vkCmdBeginRendering") == 0 ? nullptr : &DummyFn;
        };
        VkDeviceFns device;
        CHECK_FALSE(LoadDeviceFns(instance, nullptr, &device));
        CHECK(device.QueueSubmit2 == nullptr);
    }
}
