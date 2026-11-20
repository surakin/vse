#include "build_defs.h"
#if ENGINE_SANITIZER_ADDRESS
#include <sanitizer/lsan_interface.h>
#endif

#include <catch2/catch_test_macros.hpp>

#include "gpu_fixture.h"
#include "rhi/vulkan/vk_instance.h"
#include "cstr/cstr.h"

using namespace vse;
using namespace vse::rhi::vulkan;

TEST_CASE("ResolveValidationPolicy: descriptor mode, environment and build type", "[rhi][vulkan]")
{
    VulkanInstanceDesc desc;

    SECTION("Auto follows the environment, then the build type")
    {
        CHECK(ResolveValidationPolicy(desc, nullptr, nullptr, nullptr, true).enable);
        CHECK_FALSE(ResolveValidationPolicy(desc, nullptr, nullptr, nullptr, false).enable);
        CHECK(ResolveValidationPolicy(desc, "1", nullptr, nullptr, false).enable);
        CHECK_FALSE(ResolveValidationPolicy(desc, "0", nullptr, nullptr, true).enable);
        CHECK(ResolveValidationPolicy(desc, "garbage", nullptr, nullptr, true).enable);  // unrecognized == unset
    }
    SECTION("On and Off ignore the environment")
    {
        desc.validation = ValidationMode::On;
        CHECK(ResolveValidationPolicy(desc, "0", nullptr, nullptr, false).enable);
        desc.validation = ValidationMode::Off;
        CHECK_FALSE(ResolveValidationPolicy(desc, "1", nullptr, nullptr, true).enable);
    }
    SECTION("sync and best-practices: environment overrides the descriptor, and need validation on")
    {
        desc.validation = ValidationMode::On;
        desc.sync_validation = true;
        ValidationPolicy p = ResolveValidationPolicy(desc, nullptr, nullptr, nullptr, true);
        CHECK(p.sync);
        CHECK_FALSE(p.best_practices);
        p = ResolveValidationPolicy(desc, nullptr, "0", "1", true);
        CHECK_FALSE(p.sync);
        CHECK(p.best_practices);
        desc.validation = ValidationMode::Off;
        p = ResolveValidationPolicy(desc, nullptr, "1", "1", true);
        CHECK_FALSE(p.sync);
        CHECK_FALSE(p.best_practices);
    }
}

TEST_CASE("VulkanInstance creates and destroys cleanly with and without validation", "[rhi][vulkan][gpu]")
{
    SECTION("validation off")
    {
        gpu_test::GpuFixture fx;
        fx.Require();
        VulkanInstanceDesc desc;
        desc.validation = ValidationMode::Off;
        VulkanInstance off = VulkanInstance::Create(fx.Loader(), desc);
        REQUIRE(off.Valid());
        CHECK_FALSE(off.ValidationEnabled());
        CHECK(off.Fns().EnumeratePhysicalDevices != nullptr);
        CHECK_FALSE(off.Presentable());
    }
    SECTION("validation on (the fixture's own instance)")
    {
        gpu_test::GpuFixture fx;
        fx.RequireValidation();
        u32 count = 0;
        const VulkanInstance& instance = fx.Instance();
        CHECK(instance.Fns().EnumeratePhysicalDevices(instance.Handle(), &count, nullptr) == VK_SUCCESS);
    }
    SECTION("a moved-from instance is invalid and the target owns the handle")
    {
        gpu_test::GpuFixture fx;
        fx.Require();
        VulkanInstance a = VulkanInstance::Create(fx.Loader(), VulkanInstanceDesc{});
        REQUIRE(a.Valid());
        VkInstance raw = a.Handle();
        VulkanInstance b = static_cast<VulkanInstance&&>(a);
        CHECK_FALSE(a.Valid());
        CHECK(b.Handle() == raw);
    }
}

TEST_CASE("VulkanInstance enables the surface extensions only when presentable", "[rhi][vulkan][gpu]")
{
    gpu_test::GpuFixture fx(/*presentable=*/true);
    fx.Require();
    if (!fx.Instance().Presentable())
    {
        SKIP("not presentable");
    }
    CHECK(fx.Instance().Fns().DestroySurfaceKHR != nullptr);
}

namespace
{
struct Seen
{
    int errors = 0;
    char id[256] = {};
};
// Records the error count and the message text (the VUID is in the text: for this call the loader itself
// reports the error, with the generic id "Loader Message").
void SeenCapture(void* user, LogVerbosity severity, const char*, const char* text)
{
    Seen* seen = static_cast<Seen*>(user);
    if (severity == LogVerbosity::Error)
    {
        ++seen->errors;
        CStrCopyN(seen->id, text, sizeof(seen->id) - 1);
    }
}
}  // namespace

TEST_CASE("Validation messages are logged, counted and reach the capture hook", "[rhi][vulkan][gpu]")
{
    gpu_test::GpuFixture fx;
    fx.RequireValidation();
    fx.ExpectErrors(1);

    // A deliberately invalid call: a null count pointer. The layer reports it and drops the call.
    const VulkanInstance& instance = fx.Instance();
    VkResult r = instance.Fns().EnumeratePhysicalDevices(instance.Handle(), nullptr, nullptr);
    (void)r;

    CHECK(GetValidationCounts().errors == 1);
    REQUIRE(fx.Messages().Size() >= 1);
    bool found = false;
    for (usize i = 0; i < fx.Messages().Size(); ++i)
    {
        const gpu_test::CapturedMessage& m = fx.Messages()[i];
        if (m.severity == LogVerbosity::Error &&
            m.text.View().Contains(StringView("VUID-vkEnumeratePhysicalDevices-pPhysicalDeviceCount-parameter")))
        {
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("SetValidationCapture can be replaced and removed", "[rhi][vulkan][gpu]")
{
    gpu_test::GpuFixture fx;
    fx.RequireValidation();
    fx.ExpectErrors(1);
    Seen seen;
    SetValidationCapture(&SeenCapture, &seen);  // replaces the fixture's hook
    const VulkanInstance& instance = fx.Instance();
    instance.Fns().EnumeratePhysicalDevices(instance.Handle(), nullptr, nullptr);
    CHECK(seen.errors == 1);
    CHECK(CStrFind(seen.id, "VUID-vkEnumeratePhysicalDevices") != nullptr);
    CHECK(fx.Messages().Empty());  // the fixture's hook was replaced, so it saw nothing
    SetValidationCapture(nullptr, nullptr);
}

namespace
{
VKAPI_ATTR VkBool32 VKAPI_CALL NoopMessenger(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT*, void*)
{
    return VK_FALSE;
}
}  // namespace

TEST_CASE("Errors raised while the instance is destroyed are still counted", "[rhi][vulkan][gpu]")
{
    gpu_test::GpuFixture fx;
    fx.RequireValidation();
    {
        // Leak a debug messenger: vkDestroyInstance then reports an object leak, after a traditionally
        // created messenger would already be gone.
        VulkanInstanceDesc desc;
        desc.validation = ValidationMode::On;
        VulkanInstance inner = VulkanInstance::Create(fx.Loader(), desc);
        REQUIRE(inner.Valid());
        VkDebugUtilsMessengerCreateInfoEXT mci{};
        mci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        mci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        mci.pfnUserCallback = &NoopMessenger;
        VkDebugUtilsMessengerEXT leaked = nullptr;
#if ENGINE_SANITIZER_ADDRESS
        __lsan_disable();  // the leak is the point of this test; ignore only what the loader and layer allocate for it
#endif
        const VkResult created = inner.Fns().CreateDebugUtilsMessengerEXT(inner.Handle(), &mci, nullptr, &leaked);
#if ENGINE_SANITIZER_ADDRESS
        __lsan_enable();
#endif
        REQUIRE(created == VK_SUCCESS);
    }  // inner destroyed here with `leaked` still alive
    CHECK(GetValidationCounts().errors >= 1);
    fx.ExpectErrors(GetValidationCounts().errors);  // reconcile: the count is whatever the layer reported
}
