#include <cstdlib>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gpu_fixture.h"
#include "os/input.h"
#include "os/linux/platform_window_host.h"
#include "rhi/rhi.h"
#include "rhi/vulkan/vulkan_device.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::vulkan;

namespace {

bool VulkanFrameTestHaveEnv(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && v[0] != '\0';
}

// Renders a clear into an offscreen texture and reads the pixels back: this checks BeginRendering/EndRendering
// without depending on a window.
TEST_CASE("BeginRendering clears an offscreen color attachment", "[rhi][vulkan][gpu][frame]") {
    gpu_test::GpuFixture fx;
    fx.RequireValidation();
    DeviceCreateInfo info;
    info.validation = ValidationSetting::On;
    info.sync_validation = true;
    VulkanDevicePtr device = VulkanDevice::Create(info);
    if (device == nullptr) {
        SKIP("no suitable Vulkan device");
    }
    TextureDesc td;
    td.width = 32;
    td.height = 16;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
    TextureHandle tex = device->CreateTexture(td);
    REQUIRE(tex.IsValid());

    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->Barrier(tex, ResourceState::Undefined, ResourceState::ColorAttachment);
    RenderingDesc pass;
    pass.color_count = 1;
    pass.color[0].texture = tex;
    pass.color[0].load = LoadOp::Clear;
    pass.color[0].clear_color[0] = 1.0f;
    pass.color[0].clear_color[1] = 0.0f;
    pass.color[0].clear_color[2] = 0.0f;
    pass.color[0].clear_color[3] = 1.0f;
    list->BeginRendering(pass);
    list->EndRendering();
    list->End();
    CommandList* lists[] = {list};
    REQUIRE(device->Submit(lists, 1) != 0);
    device->EndFrame();

    std::vector<unsigned char> back(32 * 16 * 4);
    REQUIRE(device->ReadbackTexture(tex, 0, 0, back.data(), back.size(), ResourceState::ColorAttachment));
    bool all_red = true;
    for (usize i = 0; i < back.size(); i += 4) {
        all_red = all_red && back[i] == 255 && back[i + 1] == 0 && back[i + 2] == 0 && back[i + 3] == 255;
    }
    CHECK(all_red);
    device->DestroyTexture(tex);
    device->WaitIdle();
    device.Reset();
}

struct LoopResult {
    u64 max_in_flight = 0;
    u64 frames = 0;
    u64 final_completed = 0;
};

// Runs `frames` frames of a clear loop with `frames_in_flight` frames allowed and reports the pacing.
LoopResult RunLoop(os::WindowBackend backend, u32 frames_in_flight, u32 frames) {
    LoopResult result;
    os::InputState input;
    os::PlatformWindowHost host;
    if (!host.Open(backend, "rhi frame test", 320, 240, &input)) {
        SKIP("the window could not be opened");
    }
    os::Window* window = host.Get();
    DeviceCreateInfo info;
    info.validation = ValidationSetting::On;
    info.sync_validation = true;
    info.window = window;
    info.frames_in_flight = frames_in_flight;
    VulkanDevicePtr device = VulkanDevice::Create(info);
    if (device == nullptr) {
        if (gpu_test::GpuRequired()) {
            FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no Vulkan device could present to the window");
        }
        SKIP("no Vulkan device can present to this window");
    }
    CHECK(device->FramesInFlight() == frames_in_flight);
    Swapchain* sc = device->CreateSwapchain(SwapchainDesc{});
    REQUIRE(sc != nullptr);

    for (u32 i = 0; i < frames; ++i) {
        window->PumpEvents();
        device->BeginFrame();
        const u64 in_flight = device->FrameNumber() - device->CompletedFrame();
        if (in_flight > result.max_in_flight) {
            result.max_in_flight = in_flight;
        }
        const SwapchainStatus acquired = sc->Acquire();
        REQUIRE((acquired == SwapchainStatus::Ok || acquired == SwapchainStatus::Recreated));
        const TextureHandle image = sc->CurrentTexture();
        CommandList* list = device->CreateCommandList();
        list->Begin();
        list->Barrier(image, ResourceState::Undefined, ResourceState::ColorAttachment);
        RenderingDesc pass;
        pass.color_count = 1;
        pass.color[0].texture = image;
        pass.color[0].clear_color[0] = static_cast<f32>(i % 60) / 60.0f;
        pass.color[0].clear_color[2] = 0.5f;
        list->BeginRendering(pass);
        list->EndRendering();
        list->Barrier(image, ResourceState::ColorAttachment, ResourceState::Present);
        list->End();
        CommandList* lists[] = {list};
        REQUIRE(device->Submit(lists, 1) != 0);
        device->EndFrame();
        const SwapchainStatus presented = sc->Present();
        CHECK((presented == SwapchainStatus::Ok || presented == SwapchainStatus::Recreated));
        ++result.frames;
    }
    device->WaitIdle();
    result.final_completed = device->CompletedFrame();
    device->DestroySwapchain(sc);
    device.Reset();
    return result;
}

void RunFrameLoopChecks(os::WindowBackend backend) {
    gpu_test::GpuFixture fx(/*presentable=*/true);
    fx.Require();
    VulkanDevice::ResetLeakCounts();
    fx.ExpectErrors(0);
    const LoopResult f2 = RunLoop(backend, 2, 120);
    const LoopResult f1 = RunLoop(backend, 1, 120);
    const LoopResult f3 = RunLoop(backend, 3, 120);
    // The same loop renders the same number of frames whatever the pacing, and nothing was logged as an error.
    CHECK(f1.frames == f2.frames);
    CHECK(f3.frames == f2.frames);
    CHECK(f2.frames == 120);
    CHECK(GetValidationCounts().errors == 0);
    CHECK(GetValidationCounts().warnings == 0);
    CHECK(f2.max_in_flight <= 2);
    CHECK(f1.max_in_flight <= 1);
    CHECK(f3.max_in_flight <= 3);
    // Every flavor finishes every frame it began.
    CHECK(f1.final_completed == f1.frames);
    CHECK(f2.final_completed == f2.frames);
    CHECK(f3.final_completed == f3.frames);
    const VulkanDevice::LeakCounts leaks = VulkanDevice::GetLeakCounts();
    CHECK(leaks.user_objects == 0);
    CHECK(leaks.internal_objects == 0);
}

} // namespace

TEST_CASE("The frame loop on a Wayland window keeps frames in flight bounded and leaks nothing", "[rhi][vulkan][gpu][window][wayland][frame]") {
    if (!VulkanFrameTestHaveEnv("WAYLAND_DISPLAY")) {
        SKIP("no Wayland session");
    }
    RunFrameLoopChecks(os::WindowBackend::Wayland);
}

TEST_CASE("The frame loop on an X11 window keeps frames in flight bounded and leaks nothing", "[rhi][vulkan][gpu][window][x11][frame]") {
    if (!VulkanFrameTestHaveEnv("DISPLAY")) {
        SKIP("no X11 display");
    }
    RunFrameLoopChecks(os::WindowBackend::X11);
}
