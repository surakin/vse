// Phase 8 Day 107: swapchain reconfiguration, format and HDR choice, surface re-creation.
#include <cstring>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include "display_session.h"

#include "gpu_fixture.h"
#include <cstdlib>

#include "log/assert.h"
#include "platform/input.h"
#include "platform/window_host.h"
#include "rhi/device_host.h"
#include "rhi/null/null_device.h"
#include "rhi/rhi_log.h"
#include "rhi/vulkan/vk_swapchain_policy.h"
#include "build_defs.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using engine_placeholder::rhi::vulkan::ChooseRequestedSurfaceFormat;
using engine_placeholder::rhi::vulkan::FillSwapchainCaps;

namespace {

struct SwapQuietLog {
    bool saved = LogRhi.Enabled();
    SwapQuietLog() { LogRhi.SetEnabled(false); }
    ~SwapQuietLog() { LogRhi.SetEnabled(saved); }
};

struct SwapAssertThrown {};
AssertAction SwapThrowingHandler(const AssertInfo&) { throw SwapAssertThrown{}; }
struct SwapThrowingAsserts {
    AssertHandler previous = SetAssertHandler(&SwapThrowingHandler);
    ~SwapThrowingAsserts() { SetAssertHandler(previous); }
};

DeviceCreateInfo SwapNullInfo() {
    DeviceCreateInfo info;
    info.backend = Backend::Null;
    return info;
}

SwapchainDesc SwapSized(u32 w, u32 h) {
    SwapchainDesc d;
    d.width = w;
    d.height = h;
    return d;
}

const VkSurfaceFormatKHR kSrgb = {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
const VkSurfaceFormatKHR kRgbaSrgb = {VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
const VkSurfaceFormatKHR kUnorm = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
const VkSurfaceFormatKHR kScRgb = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT};

struct SwapObserver final : DeviceObserver {
    int changed = 0;
    Format format = Format::Undefined;
    void OnDeviceReleasing(u32) override {}
    bool OnDeviceCreated(Device*, u32) override { return true; }
    void OnSwapchainChanged(Swapchain* swapchain) override {
        ++changed;
        format = swapchain->ImageFormat();
    }
    const char* ObserverName() const override { return "swap"; }
};

} // namespace

TEST_CASE("The requested format is honored when offered and falls back with a note when not", "[rhi][swapchain][policy]") {
    const VkSurfaceFormatKHR offered[] = {kSrgb, kUnorm};
    auto choice = ChooseRequestedSurfaceFormat(offered, 2, Format::Undefined, HdrMode::Off);
    CHECK(choice.format.format == VK_FORMAT_B8G8R8A8_SRGB);
    CHECK(choice.fallback_note == nullptr);
    CHECK(choice.space == ColorSpace::SrgbNonlinear);

    choice = ChooseRequestedSurfaceFormat(offered, 2, Format::BGRA8_UNorm, HdrMode::Off);
    CHECK(choice.format.format == VK_FORMAT_B8G8R8A8_UNORM);
    CHECK(choice.fallback_note == nullptr);

    choice = ChooseRequestedSurfaceFormat(offered, 2, Format::RGBA8_SRGB, HdrMode::Off); // not offered
    CHECK(choice.format.format == VK_FORMAT_B8G8R8A8_SRGB);
    CHECK(choice.fallback_note != nullptr);
}

TEST_CASE("HDR is granted only with a matching surface format", "[rhi][swapchain][policy]") {
    const VkSurfaceFormatKHR sdr_only[] = {kSrgb};
    auto choice = ChooseRequestedSurfaceFormat(sdr_only, 1, Format::Undefined, HdrMode::ScRgbLinear);
    CHECK(choice.space == ColorSpace::SrgbNonlinear);
    CHECK(choice.fallback_note != nullptr);
    CHECK(choice.format.format == VK_FORMAT_B8G8R8A8_SRGB);

    const VkSurfaceFormatKHR with_hdr[] = {kSrgb, kScRgb};
    choice = ChooseRequestedSurfaceFormat(with_hdr, 2, Format::Undefined, HdrMode::ScRgbLinear);
    CHECK(choice.space == ColorSpace::ScRgbLinear);
    CHECK(choice.format.format == VK_FORMAT_R16G16B16A16_SFLOAT);
    CHECK(choice.fallback_note == nullptr);

    choice = ChooseRequestedSurfaceFormat(with_hdr, 2, Format::Undefined, HdrMode::Hdr10);
    CHECK(choice.space == ColorSpace::SrgbNonlinear);
    CHECK(choice.fallback_note != nullptr);
    CHECK(choice.format.format == VK_FORMAT_B8G8R8A8_SRGB);
}

TEST_CASE("Swapchain capabilities list what the surface offers", "[rhi][swapchain][policy]") {
    const VkSurfaceFormatKHR formats[] = {kSrgb, kRgbaSrgb, kSrgb, kScRgb};
    const VkPresentModeKHR modes[] = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR};
    SwapchainCaps caps;
    FillSwapchainCaps(formats, 4, modes, 2, &caps);
    CHECK(caps.present_mode_supported[0]);
    CHECK(caps.present_mode_supported[1]);
    CHECK_FALSE(caps.present_mode_supported[2]);
    CHECK(caps.hdr_scrgb);
    CHECK_FALSE(caps.hdr10);
    REQUIRE(caps.format_count == 2); // the duplicate and the HDR entry are not listed
    CHECK(caps.formats[0] == Format::BGRA8_SRGB);
    CHECK(caps.formats[1] == Format::RGBA8_SRGB);
}

TEST_CASE("A null swapchain reconfigures format, HDR and present mode and reports the result", "[rhi][swapchain]") {
    DevicePtr device = CreateDevice(SwapNullInfo());
    Swapchain* swapchain = device->CreateSwapchain(SwapSized(64, 48));
    REQUIRE(swapchain != nullptr);
    CHECK(swapchain->Width() == 64);
    CHECK(swapchain->ImageFormat() == Format::BGRA8_SRGB);
    CHECK(swapchain->GetColorSpace() == ColorSpace::SrgbNonlinear);

    SwapchainDesc desc = SwapSized(128, 96);
    desc.present_mode = PresentMode::Immediate;
    desc.format = Format::RGBA8_SRGB;
    CHECK(swapchain->Reconfigure(desc) == SwapchainStatus::Ok);
    CHECK(swapchain->Width() == 128);
    CHECK(swapchain->Height() == 96);
    CHECK(swapchain->ActualPresentMode() == PresentMode::Immediate);
    CHECK(swapchain->ImageFormat() == Format::RGBA8_SRGB);
    CHECK(swapchain->Capabilities().hdr_scrgb);

    desc.hdr = HdrMode::ScRgbLinear;
    CHECK(swapchain->Reconfigure(desc) == SwapchainStatus::Ok);
    CHECK(swapchain->ImageFormat() == Format::RGBA16_Float);
    CHECK(swapchain->GetColorSpace() == ColorSpace::ScRgbLinear);

    // The new images work: acquire and present one.
    device->BeginFrame();
    REQUIRE(swapchain->Acquire() == SwapchainStatus::Ok);
    CHECK(swapchain->CurrentTexture().IsValid());
    CHECK(swapchain->Present() == SwapchainStatus::Ok);
    device->EndFrame();
    device->DestroySwapchain(swapchain);
}

TEST_CASE("A zero extent defers the swapchain and a later size brings it back", "[rhi][swapchain]") {
    DevicePtr device = CreateDevice(SwapNullInfo());
    Swapchain* swapchain = device->CreateSwapchain(SwapSized(64, 48));
    REQUIRE(swapchain != nullptr);
    CHECK(swapchain->Resize(0, 0) == SwapchainStatus::Minimized);
    device->BeginFrame();
    CHECK(swapchain->Acquire() == SwapchainStatus::Minimized); // no zero-sized image exists
    device->EndFrame();
    CHECK(device->Resources().textures == 0);
    CHECK(swapchain->Resize(80, 60) == SwapchainStatus::Ok);
    CHECK(device->Resources().textures == 3);
    device->DestroySwapchain(swapchain);
}

TEST_CASE("Reconfigure while an image is acquired is rejected", "[rhi][swapchain]") {
    SwapQuietLog quiet;
    SwapThrowingAsserts asserts;
    DevicePtr device = CreateDevice(SwapNullInfo());
    Swapchain* swapchain = device->CreateSwapchain(SwapSized(64, 48));
    REQUIRE(swapchain != nullptr);
    device->BeginFrame();
    REQUIRE(swapchain->Acquire() == SwapchainStatus::Ok);
    bool rejected = false;
    try {
        swapchain->Reconfigure(SwapSized(32, 32));
    } catch (const SwapAssertThrown&) {
        rejected = true;
    }
    CHECK(rejected);
    CHECK(swapchain->Present() == SwapchainStatus::Ok);
    device->EndFrame();
    device->DestroySwapchain(swapchain);
}

TEST_CASE("The host reconfigures without touching the device and tells observers only about a format change", "[rhi][swapchain][host]") {
    SwapQuietLog quiet;
    SwapObserver observer;
    DeviceHost host;
    host.AddObserver(&observer, 3);
    REQUIRE(host.Create(SwapNullInfo(), SwapSized(64, 48)));
    REQUIRE(host.GetSwapchain() != nullptr);
    const u32 epoch = host.Epoch();

    SwapchainDesc desc = SwapSized(100, 80);
    desc.present_mode = PresentMode::Mailbox;
    CHECK(host.ReconfigureSwapchain(desc) == SwapchainStatus::Ok);
    CHECK(observer.changed == 0); // size and mode only
    CHECK(host.Epoch() == epoch);
    CHECK(host.GetSwapchain()->Width() == 100);

    desc.hdr = HdrMode::ScRgbLinear;
    CHECK(host.ReconfigureSwapchain(desc) == SwapchainStatus::Ok);
    CHECK(observer.changed == 1);
    CHECK(observer.format == Format::RGBA16_Float);
    CHECK(host.Epoch() == epoch); // still the same device
    host.Destroy();
}

TEST_CASE("The host refuses to reconfigure inside a frame", "[rhi][swapchain][host]") {
    SwapQuietLog quiet;
    SwapThrowingAsserts asserts;
    DeviceHost host;
    REQUIRE(host.Create(SwapNullInfo(), SwapSized(64, 48)));
    host.GetDevice()->BeginFrame();
    bool rejected = false;
    try {
        host.ReconfigureSwapchain(SwapSized(32, 32));
    } catch (const SwapAssertThrown&) {
        rejected = true;
    }
    CHECK(rejected);
    host.GetDevice()->EndFrame();
}

TEST_CASE("A lost surface is replaced without a device reset", "[rhi][swapchain][host]") {
    SwapQuietLog quiet;
    SwapObserver observer;
    DeviceHost host;
    host.AddObserver(&observer, 0);
    REQUIRE(host.Create(SwapNullInfo(), SwapSized(64, 48)));
    const u32 epoch = host.Epoch();
    auto* null_device = static_cast<NullDevice*>(host.GetDevice());

    null_device->SimulateSurfaceLoss();
    null_device->BeginFrame();
    CHECK(host.GetSwapchain()->Acquire() == SwapchainStatus::Lost);
    null_device->EndFrame();

    REQUIRE(host.RecreateSurface(nullptr, SwapSized(64, 48)));
    CHECK(host.Epoch() == epoch); // same device
    CHECK(host.SurfaceFallbackCount() == 0);
    CHECK(observer.changed == 0); // the format did not change, so nobody rebuilds anything
    host.GetDevice()->BeginFrame();
    CHECK(host.GetSwapchain()->Acquire() == SwapchainStatus::Ok);
    CHECK(host.GetSwapchain()->Present() == SwapchainStatus::Ok);
    host.GetDevice()->EndFrame();
}

TEST_CASE("When the adapter cannot present to the new surface the host does a full reset", "[rhi][swapchain][host]") {
    SwapQuietLog quiet;
    DeviceHost host;
    DeviceCreateInfo info = SwapNullInfo();
    info.faults.fail_surface_recreate = true;
    REQUIRE(host.Create(info, SwapSized(64, 48)));
    const u32 epoch = host.Epoch();
    REQUIRE(host.RecreateSurface(nullptr, SwapSized(64, 48)));
    CHECK(host.Epoch() != epoch);
    CHECK(host.SurfaceFallbackCount() == 1);
    CHECK(host.ResetCount() == 1);
    CHECK(host.GetSwapchain() != nullptr);
}

TEST_CASE("A Vulkan swapchain format request without a window fails cleanly", "[rhi][swapchain][vulkan][gpu]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    SwapQuietLog quiet;
    DeviceCreateInfo info;
    DevicePtr device = CreateDevice(info);
    if (device == nullptr) {
        SKIP("no suitable Vulkan device");
    }
    CHECK(device->CreateSwapchain(SwapchainDesc{}) == nullptr); // headless devices have no surface
}

namespace {

void RunWindowReconfigure(os::WindowBackend backend) {
    os::InputState input;
    os::PlatformWindowHost window_host;
    if (!window_host.Open(backend, "rhi reconfigure test", 320, 240, &input)) {
        SKIP("the window could not be opened");
    }
    os::Window* window = window_host.Get();
    REQUIRE(window != nullptr);

    gpu_test::GpuFixture fx(/*presentable=*/true);
    fx.Require();
    DeviceCreateInfo info;
    info.validation = ValidationSetting::On;
    info.sync_validation = true;
    info.window = window;
    DeviceHost host;
    if (!host.Create(info, SwapchainDesc{})) {
        if (gpu_test::GpuRequired()) {
            FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no Vulkan device could present to the window");
        }
        SKIP("no Vulkan device can present to this window");
    }
    fx.ExpectErrors(0);
    auto frame = [&]() {
        window->PumpEvents();
        Device* device = host.GetDevice();
        Swapchain* sc = host.GetSwapchain();
        device->BeginFrame();
        const SwapchainStatus acquired = sc->Acquire();
        REQUIRE((acquired == SwapchainStatus::Ok || acquired == SwapchainStatus::Recreated));
        CommandList* list = device->CreateCommandList();
        list->Begin();
        list->Barrier(sc->CurrentTexture(), ResourceState::Undefined, ResourceState::Present);
        list->End();
        CommandList* lists[] = {list};
        REQUIRE(device->Submit(lists, 1) != 0);
        device->EndFrame();
        const SwapchainStatus presented = sc->Present();
        CHECK((presented == SwapchainStatus::Ok || presented == SwapchainStatus::Recreated));
    };
    for (int i = 0; i < 5; ++i) {
        frame();
    }
    const u32 epoch = host.Epoch();

    // Present mode: the result is readable and rendering continues.
    const SwapchainCaps caps = host.GetSwapchain()->Capabilities();
    SwapchainDesc desc;
    desc.present_mode = PresentMode::Immediate;
    REQUIRE(host.ReconfigureSwapchain(desc) == SwapchainStatus::Ok);
    CHECK(host.GetSwapchain()->ActualPresentMode() ==
          (caps.present_mode_supported[static_cast<usize>(PresentMode::Immediate)] ? PresentMode::Immediate : PresentMode::Fifo));
    for (int i = 0; i < 3; ++i) {
        frame();
    }

    // An unsupported HDR request falls back and says so (this machine's surface rarely offers scRGB).
    desc.hdr = HdrMode::Hdr10;
    REQUIRE(host.ReconfigureSwapchain(desc) == SwapchainStatus::Ok);
    CHECK(host.GetSwapchain()->GetColorSpace() == ColorSpace::SrgbNonlinear);
    CHECK_FALSE(caps.hdr10);
    frame();

    // A format request the surface offers is honored; the actual format is reported.
    if (caps.format_count > 0) {
        desc.hdr = HdrMode::Off;
        desc.format = caps.formats[caps.format_count - 1];
        REQUIRE(host.ReconfigureSwapchain(desc) == SwapchainStatus::Ok);
        CHECK(host.GetSwapchain()->ImageFormat() == desc.format);
        frame();
    }

    // The surface is replaced without a device reset.
    REQUIRE(host.RecreateSurface(window, desc));
    CHECK(host.Epoch() == epoch);
    CHECK(host.SurfaceFallbackCount() == 0);
    for (int i = 0; i < 3; ++i) {
        frame();
    }
    host.Destroy();
}

} // namespace

TEST_CASE("A Wayland swapchain reconfigures and its surface is replaced", "[rhi][vulkan][gpu][window][wayland][swapchain]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    if (std::getenv("WAYLAND_DISPLAY") == nullptr) {
        SKIP("no Wayland session");
    }
    RunWindowReconfigure(os::WindowBackend::Wayland);
}

TEST_CASE("An X11 swapchain reconfigures and its surface is replaced", "[rhi][vulkan][gpu][window][x11][swapchain]") {
    SKIP_IF_NO_DISPLAY_SESSION();
    if (std::getenv("DISPLAY") == nullptr) {
        SKIP("no X11 display");
    }
    RunWindowReconfigure(os::WindowBackend::X11);
}

// Phase 13 Day 160: the same checks through the Win32 window and the VK_KHR_win32_surface surface.
TEST_CASE("A Win32 swapchain reconfigures and its surface is replaced", "[rhi][vulkan][gpu][window][win32][swapchain]") {
#if ENGINE_PLATFORM_WINDOWS
    SKIP_IF_NO_DISPLAY_SESSION();
    RunWindowReconfigure(os::WindowBackend::Win32);
#else
    SKIP("Win32 is the Windows backend");
#endif
}
