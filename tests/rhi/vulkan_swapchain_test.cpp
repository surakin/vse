#include <cstdlib>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include "display_session.h"

#include "gpu_fixture.h"
#include "platform/input.h"
#include "platform/window_host.h"
#include "rhi/null/null_device.h"
#include "rhi/rhi.h"
#include "rhi/vulkan/vk_swapchain_policy.h"
#include "rhi/vulkan/vulkan_device.h"
#include "build_defs.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::vulkan;

namespace {

VkSurfaceFormatKHR Fmt(VkFormat f, VkColorSpaceKHR cs = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { return VkSurfaceFormatKHR{f, cs}; }

bool VulkanSwapchainTestHaveEnv(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && v[0] != '\0';
}

} // namespace

TEST_CASE("ChooseSurfaceFormat prefers sRGB BGRA, then sRGB RGBA, then anything the RHI knows", "[rhi][swapchain][policy]") {
    SECTION("BGRA sRGB wins over RGBA sRGB wherever it is listed") {
        const VkSurfaceFormatKHR formats[] = {Fmt(VK_FORMAT_R8G8B8A8_SRGB), Fmt(VK_FORMAT_B8G8R8A8_UNORM), Fmt(VK_FORMAT_B8G8R8A8_SRGB)};
        const SurfaceFormatChoice c = ChooseSurfaceFormat(formats, 3);
        CHECK(c.format.format == VK_FORMAT_B8G8R8A8_SRGB);
        CHECK(c.preferred);
    }
    SECTION("RGBA sRGB when there is no BGRA sRGB") {
        const VkSurfaceFormatKHR formats[] = {Fmt(VK_FORMAT_B8G8R8A8_UNORM), Fmt(VK_FORMAT_R8G8B8A8_SRGB)};
        const SurfaceFormatChoice c = ChooseSurfaceFormat(formats, 2);
        CHECK(c.format.format == VK_FORMAT_R8G8B8A8_SRGB);
        CHECK(c.preferred);
    }
    SECTION("a sRGB format in another color space is not preferred") {
        const VkSurfaceFormatKHR formats[] = {Fmt(VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT), Fmt(VK_FORMAT_B8G8R8A8_UNORM)};
        const SurfaceFormatChoice c = ChooseSurfaceFormat(formats, 2);
        CHECK_FALSE(c.preferred);
        CHECK(c.format.format == VK_FORMAT_B8G8R8A8_UNORM); // sRGB-nonlinear beats the first entry in another color space
        const VkSurfaceFormatKHR only_hdr[] = {Fmt(VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_HDR10_ST2084_EXT)};
        CHECK(ChooseSurfaceFormat(only_hdr, 1).format.format == VK_FORMAT_B8G8R8A8_UNORM); // better than nothing
    }
    SECTION("formats the RHI cannot describe are skipped, and nothing usable gives UNDEFINED") {
        const VkSurfaceFormatKHR odd[] = {Fmt(VK_FORMAT_A2B10G10R10_UNORM_PACK32), Fmt(VK_FORMAT_R8G8B8A8_UNORM)};
        const SurfaceFormatChoice c = ChooseSurfaceFormat(odd, 2);
        CHECK(c.format.format == VK_FORMAT_R8G8B8A8_UNORM);
        CHECK_FALSE(c.preferred);
        CHECK(ChooseSurfaceFormat(odd, 1).format.format == VK_FORMAT_UNDEFINED);
        CHECK(ChooseSurfaceFormat(nullptr, 0).format.format == VK_FORMAT_UNDEFINED);
    }
}

TEST_CASE("ChoosePresentMode defaults to FIFO and honors requests only when offered", "[rhi][swapchain][policy]") {
    const VkPresentModeKHR all[] = {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR};
    const VkPresentModeKHR fifo_only[] = {VK_PRESENT_MODE_FIFO_KHR};
    CHECK(ChoosePresentMode(all, 3, PresentMode::Fifo) == VK_PRESENT_MODE_FIFO_KHR);
    CHECK(ChoosePresentMode(all, 3, PresentMode::Mailbox) == VK_PRESENT_MODE_MAILBOX_KHR);
    CHECK(ChoosePresentMode(all, 3, PresentMode::Immediate) == VK_PRESENT_MODE_IMMEDIATE_KHR);
    CHECK(ChoosePresentMode(fifo_only, 1, PresentMode::Mailbox) == VK_PRESENT_MODE_FIFO_KHR);
    CHECK(ChoosePresentMode(fifo_only, 1, PresentMode::Immediate) == VK_PRESENT_MODE_FIFO_KHR);
    CHECK(ChoosePresentMode(nullptr, 0, PresentMode::Fifo) == VK_PRESENT_MODE_FIFO_KHR);
}

TEST_CASE("ChooseImageCount is min + 1 capped by max", "[rhi][swapchain][policy]") {
    CHECK(ChooseImageCount(2, 8) == 3);
    CHECK(ChooseImageCount(2, 0) == 3);  // 0 = no maximum
    CHECK(ChooseImageCount(3, 3) == 3);  // capped
    CHECK(ChooseImageCount(1, 1) == 1);
}

TEST_CASE("ChooseExtent uses the fixed extent, or clamps the window size when it is undefined", "[rhi][swapchain][policy]") {
    VkSurfaceCapabilitiesKHR caps{};
    caps.minImageExtent = {16, 16};
    caps.maxImageExtent = {4096, 2048};

    SECTION("a fixed current extent wins (X11)") {
        caps.currentExtent = {800, 600};
        const VkExtent2D e = ChooseExtent(caps, 1000, 1000);
        CHECK(e.width == 800);
        CHECK(e.height == 600);
    }
    SECTION("an undefined extent (Wayland) takes the window size") {
        caps.currentExtent = {0xFFFFFFFFu, 0xFFFFFFFFu};
        const VkExtent2D e = ChooseExtent(caps, 640, 480);
        CHECK(e.width == 640);
        CHECK(e.height == 480);
    }
    SECTION("the window size is clamped into the limits") {
        caps.currentExtent = {0xFFFFFFFFu, 0xFFFFFFFFu};
        const VkExtent2D big = ChooseExtent(caps, 10000, 10000);
        CHECK(big.width == 4096);
        CHECK(big.height == 2048);
        const VkExtent2D small = ChooseExtent(caps, 4, 4);
        CHECK(small.width == 16);
        CHECK(small.height == 16);
    }
    SECTION("a zero-sized window stays zero: it is minimized") {
        caps.currentExtent = {0xFFFFFFFFu, 0xFFFFFFFFu};
        const VkExtent2D e = ChooseExtent(caps, 0, 480);
        CHECK(e.width == 0);
        CHECK(e.height == 0);
        caps.currentExtent = {0, 0};
        CHECK(ChooseExtent(caps, 640, 480).width == 0);
    }
}

TEST_CASE("ChooseCompositeAlpha and ChooseImageUsage", "[rhi][swapchain][policy]") {
    CHECK(ChooseCompositeAlpha(VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR | VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) == VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);
    CHECK(ChooseCompositeAlpha(VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) == VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR);
    CHECK(ChooseCompositeAlpha(VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR | VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) ==
          VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR);
    CHECK(ChooseCompositeAlpha(0) == VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);

    CHECK(ChooseImageUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT) ==
          (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
    CHECK(ChooseImageUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    CHECK(ChooseImageUsage(VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0);
}

TEST_CASE("Null device swapchain rotates images and reports minimize", "[rhi][null][swapchain]") {
    NullDevicePtr device = CreateNullDevice();
    Swapchain* sc = device->CreateSwapchain(SwapchainDesc{});
    REQUIRE(sc != nullptr);
    CHECK(sc->Width() == 640);
    CHECK_FALSE(sc->CurrentTexture().IsValid());
    TextureHandle seen[3];
    device->BeginFrame();
    for (int i = 0; i < 3; ++i) {
        REQUIRE(sc->Acquire() == SwapchainStatus::Ok);
        seen[i] = sc->CurrentTexture();
        CHECK(seen[i].IsValid());
        CHECK(sc->Present() == SwapchainStatus::Ok);
    }
    CHECK_FALSE(seen[0] == seen[1]);
    CHECK_FALSE(seen[1] == seen[2]);
    CHECK(sc->Resize(0, 0) == SwapchainStatus::Minimized);
    CHECK(sc->Acquire() == SwapchainStatus::Minimized);
    CHECK(sc->Resize(320, 200) == SwapchainStatus::Ok);
    CHECK(sc->Width() == 320);
    CHECK(sc->Acquire() == SwapchainStatus::Ok);
    sc->Present();
    device->EndFrame();
    CHECK(device->CreateSwapchain(SwapchainDesc{}) == nullptr); // one per device, as on Vulkan
    CHECK(device->ErrorCount() == 1);
    device->ResetErrors();
    device->DestroySwapchain(sc);
    CHECK(device->ErrorCount() == 0);
    device.Reset();
}

TEST_CASE("Null device destroys a swapchain the application forgot", "[rhi][null][swapchain]") {
    NullDevicePtr device = CreateNullDevice();
    Swapchain* sc = device->CreateSwapchain(SwapchainDesc{});
    REQUIRE(sc != nullptr);
    device.Reset(); // logs an error, frees the swapchain and its textures (checked by ASan)
}

namespace {

// Opens a window on `backend`, builds a device and swapchain for it, and runs the checks common to both backends.
void RunPresentationChecks(os::WindowBackend backend) {
    os::InputState input;
    os::PlatformWindowHost host;
    if (!host.Open(backend, "rhi swapchain test", 320, 240, &input)) {
        SKIP("the window could not be opened");
    }
    os::Window* window = host.Get();
    REQUIRE(window != nullptr);

    gpu_test::GpuFixture fx(/*presentable=*/true);
    fx.Require();
    DeviceCreateInfo info;
    info.validation = ValidationSetting::On;
    info.sync_validation = true;
    info.window = window;
    VulkanDevicePtr device = VulkanDevice::Create(info);
    if (device == nullptr) {
        if (gpu_test::GpuRequired()) {
            FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no Vulkan device could present to the window");
        }
        SKIP("no Vulkan device can present to this window");
    }

    SwapchainDesc desc;
    Swapchain* sc = device->CreateSwapchain(desc);
    REQUIRE(sc != nullptr);
    CHECK(device->CreateSwapchain(desc) == nullptr); // one swapchain per device
    fx.ExpectErrors(0);
    CHECK(sc->Width() > 0);
    CHECK(sc->Height() > 0);
    CHECK(sc->ImageFormat() != Format::Undefined);

    // One frame: acquire, move the image to Present, present.
    auto frame = [&]() {
        window->PumpEvents();
        device->BeginFrame();
        const SwapchainStatus acquired = sc->Acquire();
        REQUIRE((acquired == SwapchainStatus::Ok || acquired == SwapchainStatus::Recreated));
        const TextureHandle image = sc->CurrentTexture();
        REQUIRE(image.IsValid());
        CommandList* list = device->CreateCommandList();
        list->Begin();
        list->Barrier(image, ResourceState::Undefined, ResourceState::Present);
        list->End();
        CommandList* lists[] = {list};
        REQUIRE(device->Submit(lists, 1) != 0);
        device->EndFrame();
        const SwapchainStatus presented = sc->Present();
        CHECK((presented == SwapchainStatus::Ok || presented == SwapchainStatus::Recreated));
    };
    for (int i = 0; i < 10; ++i) {
        frame();
    }

    // Resize twice: the images follow the surface's extent.
    window->PumpEvents();
    CHECK(sc->Resize(200, 150) == SwapchainStatus::Ok);
    const u32 w1 = sc->Width();
    CHECK(w1 > 0);
    if (std::string(window->BackendName()) == "wayland") {
        CHECK(w1 == 200); // Wayland leaves the extent to the swapchain
        CHECK(sc->Height() == 150);
    } else {
        CHECK(w1 == window->Width()); // X11 fixes it to the window
    }
    CHECK(sc->Resize(260, 180) == SwapchainStatus::Ok);
    CHECK(sc->Width() > 0);

    // Minimize: a zero size reports it, nothing is created for it, and recovery works.
    CHECK(sc->Resize(0, 0) == SwapchainStatus::Minimized);
    frame(); // the window itself is not minimized, so Acquire rebuilds the swapchain (status Recreated) and renders

    device->DestroySwapchain(sc);
    device->WaitIdle();
    CHECK(device->Counts().textures == 0);
    device.Reset();
}

} // namespace

TEST_CASE("Vulkan swapchain presents to a Wayland window", "[rhi][vulkan][gpu][window][wayland]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    if (!VulkanSwapchainTestHaveEnv("WAYLAND_DISPLAY")) {
        SKIP("no Wayland session");
    }
    RunPresentationChecks(os::WindowBackend::Wayland);
}

TEST_CASE("Vulkan swapchain presents to an X11 window", "[rhi][vulkan][gpu][window][x11]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    if (!VulkanSwapchainTestHaveEnv("DISPLAY")) {
        SKIP("no X11 display");
    }
    RunPresentationChecks(os::WindowBackend::X11);
}

// Phase 13 Day 160: the same checks through the Win32 window and the VK_KHR_win32_surface surface.
TEST_CASE("Vulkan swapchain presents to a Win32 window", "[rhi][vulkan][gpu][window][win32]") {
#if ENGINE_PLATFORM_WINDOWS
    SKIP_IF_NO_DISPLAY_SESSION();
    RunPresentationChecks(os::WindowBackend::Win32);
#else
    SKIP("Win32 is the Windows backend");
#endif
}

TEST_CASE("Vulkan device without a window refuses a swapchain", "[rhi][vulkan][gpu][swapchain]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    DeviceCreateInfo info;
    info.validation = ValidationSetting::On;
    VulkanDevicePtr device = VulkanDevice::Create(info);
    if (device == nullptr) {
        SKIP("no suitable Vulkan device");
    }
    CHECK(device->CreateSwapchain(SwapchainDesc{}) == nullptr);
    device->DestroySwapchain(nullptr); // no effect
    device.Reset();
}
