// The Direct3D 12 swapchain on a Win32 window (Phase 13 Day 169): the DeviceHost flow the Vulkan swapchains run
// (present mode, HDR and format requests, a replaced surface), the tearing flag and Immediate, the frame-latency wait,
// the sRGB view of a linear buffer, a window with no area, and a device lost at present. Windows only; skipped when the
// session cannot show windows.
#include "lang/scalar.h"

#include <catch2/catch_test_macros.hpp>

#include "display_session.h"
#include "platform/input.h"
#include "platform/window.h"
#include "platform/window_host.h"
#include "rhi/device_host.h"
#include "rhi/dx12/dx12_debug.h"
#include "rhi/dx12/dx12_device.h"
#include "rhi/dx12/dx12_swapchain.h"
#include "os/os.h"
#include "rhi/rhi.h"

using namespace vse;
using namespace vse::rhi;
using namespace vse::rhi::dx12;

namespace dx12_swapchain_test
{

struct SwapchainSilence
{
    SwapchainSilence() { ResetDebugMessageCounts(); }
    ~SwapchainSilence()
    {
        const DebugMessageCounts c = GetDebugMessageCounts();
        CHECK(c.errors == 0);
        CHECK(c.warnings == 0);
    }
};

DeviceCreateInfo Dx12Info(const os::Window* window)
{
    DeviceCreateInfo info;
    info.backend = Backend::Dx12;
    info.application_name = "dx12-swapchain-test";
    info.validation = ValidationSetting::On;
    info.window = window;
    return info;
}

// One frame: acquire, clear the image to `linear` (through its view), move it to Present, submit, present.
SwapchainStatus Frame(Device* device, Swapchain* sc, f32 linear = 0.5f)
{
    device->BeginFrame();
    const SwapchainStatus acquired = sc->Acquire();
    if (acquired != SwapchainStatus::Ok && acquired != SwapchainStatus::Recreated)
    {
        device->EndFrame();
        return acquired;
    }
    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->Barrier(sc->CurrentTexture(), ResourceState::Undefined, ResourceState::ColorAttachment);
    RenderingDesc pass;
    pass.color_count = 1;
    pass.color[0].texture = sc->CurrentTexture();
    pass.color[0].clear_color[0] = pass.color[0].clear_color[1] = pass.color[0].clear_color[2] = linear;
    list->BeginRendering(pass);
    list->EndRendering();
    list->Barrier(sc->CurrentTexture(), ResourceState::ColorAttachment, ResourceState::Present);
    list->End();
    CommandList* lists[] = {list};
    device->Submit(lists, 1);
    device->EndFrame();
    return sc->Present();
}

}  // namespace dx12_swapchain_test

using namespace dx12_swapchain_test;

TEST_CASE("A DX12 swapchain on a Win32 window reconfigures through the device host, and its window is replaced",
          "[rhi][dx12][gpu][window][swapchain]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    os::PlatformWindowHost window_host;
    REQUIRE(window_host.Open(os::WindowBackend::Win32, "dx12 swapchain test", 320, 240, &input));
    os::Window* window = window_host.Get();
    SwapchainSilence silence;
    DeviceHost host;
    REQUIRE(host.Create(Dx12Info(window), SwapchainDesc{}));
    CHECK(StringView(host.GetDevice()->BackendName()) == StringView("dx12"));
    auto frame = [&]()
    {
        window->PumpEvents();
        const SwapchainStatus s = Frame(host.GetDevice(), host.GetSwapchain());
        CHECK((s == SwapchainStatus::Ok || s == SwapchainStatus::Recreated));
    };
    for (int i = 0; i < 5; ++i)
    {
        frame();
    }
    CHECK(host.GetSwapchain()->Width() == window->Width());
    CHECK(host.GetSwapchain()->ImageFormat() == Format::BGRA8_SRGB);
    const u32 epoch = host.Epoch();

    const SwapchainCaps caps = host.GetSwapchain()->Capabilities();
    CHECK(caps.present_mode_supported[static_cast<usize>(PresentMode::Fifo)]);
    CHECK_FALSE(caps.present_mode_supported[static_cast<usize>(PresentMode::Mailbox)]);
    SwapchainDesc desc;
    desc.present_mode = PresentMode::Immediate;
    REQUIRE(host.ReconfigureSwapchain(desc) == SwapchainStatus::Ok);
    CHECK(host.GetSwapchain()->ActualPresentMode() ==
          (caps.present_mode_supported[static_cast<usize>(PresentMode::Immediate)] ? PresentMode::Immediate
                                                                                   : PresentMode::Fifo));
    for (int i = 0; i < 3; ++i)
    {
        frame();
    }
    desc.present_mode = PresentMode::Mailbox;
    REQUIRE(host.ReconfigureSwapchain(desc) == SwapchainStatus::Ok);
    CHECK(host.GetSwapchain()->ActualPresentMode() == PresentMode::Fifo);
    frame();

    desc.hdr = HdrMode::Hdr10;  // never granted: no 10-bit RHI format
    REQUIRE(host.ReconfigureSwapchain(desc) == SwapchainStatus::Ok);
    CHECK(host.GetSwapchain()->GetColorSpace() == ColorSpace::SrgbNonlinear);
    CHECK_FALSE(caps.hdr10);
    frame();
    desc.hdr = HdrMode::ScRgbLinear;  // granted exactly when the capabilities say it can be
    REQUIRE(host.ReconfigureSwapchain(desc) == SwapchainStatus::Ok);
    const bool scrgb = host.GetSwapchain()->Capabilities().hdr_scrgb;
    CHECK((host.GetSwapchain()->GetColorSpace() == ColorSpace::ScRgbLinear) == scrgb);
    CHECK((host.GetSwapchain()->ImageFormat() == Format::RGBA16_Float) == scrgb);
    {
        // What the display itself is set to: an HDR desktop shows ST 2084 (G2084) as the output's color space.
        auto* d3d = static_cast<Dx12Device*>(host.GetDevice());
        ComRef<IDXGIAdapter1> adapter;
        ComRef<IDXGIOutput> output;
        ComRef<IDXGIOutput6> output6;
        DXGI_OUTPUT_DESC1 od{};
        const bool known = SUCCEEDED(d3d->Factory()->EnumAdapters1(0, adapter.Put())) &&
                           SUCCEEDED(adapter->EnumOutputs(0, output.Put())) &&
                           SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), output6.PutVoid())) &&
                           SUCCEEDED(output6->GetDesc1(&od));
        WARN("scRGB presentable: " << (scrgb ? "yes" : "no") << "; first output's color space "
                                   << (known ? (od.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
                                                    ? "HDR (G2084)"
                                                    : "SDR")
                                             : "unknown")
                                   << ", max luminance " << (known ? od.MaxLuminance : 0.0f) << " nits");
    }
    frame();

    desc.hdr = HdrMode::Off;
    for (u32 i = 0; i < caps.format_count; ++i)
    {
        desc.format = caps.formats[i];
        INFO(FormatInfo(desc.format).name);
        REQUIRE(host.ReconfigureSwapchain(desc) == SwapchainStatus::Ok);
        CHECK(host.GetSwapchain()->ImageFormat() == desc.format);
        frame();
    }
    desc.format = Format::R8_UNorm;  // not presentable: falls back
    REQUIRE(host.ReconfigureSwapchain(desc) == SwapchainStatus::Ok);
    CHECK(host.GetSwapchain()->ImageFormat() == Format::BGRA8_SRGB);
    frame();

    REQUIRE(host.RecreateSurface(window, desc));
    CHECK(host.Epoch() == epoch);  // no device reset
    for (int i = 0; i < 3; ++i)
    {
        frame();
    }
    host.Destroy();
}

TEST_CASE(
    "A DX12 swapchain's sRGB image is a view of a linear buffer, with tearing and the latency object as the system allows",
    "[rhi][dx12][gpu][window][swapchain]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    os::PlatformWindowHost window_host;
    REQUIRE(window_host.Open(os::WindowBackend::Win32, "dx12 swapchain view", 64, 64, &input));
    os::Window* window = window_host.Get();
    SwapchainSilence silence;
    Dx12DevicePtr device = Dx12Device::Create(Dx12Info(window));
    REQUIRE(device);
    auto* sc = static_cast<Dx12Swapchain*>(device->CreateSwapchain(SwapchainDesc{}));
    REQUIRE(sc != nullptr);
    CHECK(sc->BufferFormat() == DXGI_FORMAT_B8G8R8A8_UNORM);
    CHECK(sc->ImageFormat() == Format::BGRA8_SRGB);
    CHECK(sc->LatencyWaitable() != nullptr);
    CHECK(sc->Capabilities().present_mode_supported[static_cast<usize>(PresentMode::Immediate)] ==
          sc->TearingAllowed());
    BOOL tearing = FALSE;
    const bool system_tearing = SUCCEEDED(device->Factory()->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                                                 &tearing, sizeof(tearing))) &&
                                tearing;
    CHECK(sc->TearingAllowed() == system_tearing);
    WARN("tearing (Immediate) on this PC: " << (system_tearing ? "allowed" : "not allowed"));
    CHECK(device->Resources().swapchains == 1);

    // Clear to linear 0.5 through the sRGB view: the linear buffer holds 0.5 encoded, 188.
    window->PumpEvents();
    device->BeginFrame();
    REQUIRE(sc->Acquire() != SwapchainStatus::Lost);
    const TextureHandle image = sc->CurrentTexture();
    REQUIRE(image.IsValid());
    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->Barrier(image, ResourceState::Undefined, ResourceState::ColorAttachment);
    RenderingDesc pass;
    pass.color_count = 1;
    pass.color[0].texture = image;
    pass.color[0].clear_color[0] = pass.color[0].clear_color[1] = pass.color[0].clear_color[2] = 0.5f;
    list->BeginRendering(pass);
    list->EndRendering();
    list->Barrier(image, ResourceState::ColorAttachment, ResourceState::CopySrc);
    list->End();
    CommandList* lists[] = {list};
    REQUIRE(device->Submit(lists, 1) != 0);
    device->EndFrame();
    device->WaitIdle();
    u8 px[4] = {};
    const u32 w = sc->Width(), h = sc->Height();
    Vector<u8> pixels;
    pixels.Resize(static_cast<usize>(w) * h * 4);
    REQUIRE(device->ReadbackTexture(image, 0, 0, pixels.Data(), pixels.Size(), ResourceState::CopySrc));
    for (int c = 0; c < 4; ++c)
    {
        px[c] = pixels[c];
    }
    CHECK(Abs(px[0] - 188) <= 1);
    CHECK(Abs(px[1] - 188) <= 1);
    CHECK(Abs(px[2] - 188) <= 1);
    device->BeginFrame();
    list = device->CreateCommandList();
    list->Begin();
    list->Barrier(image, ResourceState::CopySrc, ResourceState::Present);
    list->End();
    lists[0] = list;
    REQUIRE(device->Submit(lists, 1) != 0);
    device->EndFrame();
    CHECK(sc->Present() == SwapchainStatus::Ok);
    device->DestroySwapchain(sc);
    CHECK(device->Resources().swapchains == 0);
    device->Quiesce();
}

TEST_CASE("A DX12 swapchain skips a minimized window without waiting and is rebuilt when it comes back",
          "[rhi][dx12][gpu][window][swapchain]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    os::PlatformWindowHost window_host;
    REQUIRE(window_host.Open(os::WindowBackend::Win32, "dx12 swapchain minimize", 200, 150, &input));
    os::Window* window = window_host.Get();
    SwapchainSilence silence;
    Dx12DevicePtr device = Dx12Device::Create(Dx12Info(window));
    REQUIRE(device);
    Swapchain* sc = device->CreateSwapchain(SwapchainDesc{});
    REQUIRE(sc != nullptr);
    window->PumpEvents();
    CHECK(Frame(device.Get(), sc) == SwapchainStatus::Ok);
    // A zero size is remembered: nothing to draw to until the window has an area.
    CHECK(sc->Resize(0, 0) == SwapchainStatus::Minimized);
    const HWND hwnd = static_cast<HWND>(window->GetNativeInfo().surface);
    ShowWindow(hwnd, SW_MINIMIZE);
    for (int i = 0; i < 50 && !window->IsMinimized(); ++i)
    {
        window->PumpEvents();
    }
    REQUIRE(window->IsMinimized());
    const u64 start = os::MonotonicTimeNanos();
    for (int i = 0; i < 20; ++i)
    {
        device->BeginFrame();
        CHECK(sc->Acquire() == SwapchainStatus::Minimized);
        device->EndFrame();
    }
    const f64 ms = (static_cast<f64>(os::MonotonicTimeNanos() - start) / 1e6);
    CHECK(ms < 200.0);  // twenty skipped frames, no waits
    ShowWindow(hwnd, SW_RESTORE);
    for (int i = 0; i < 50 && window->IsMinimized(); ++i)
    {
        window->PumpEvents();
    }
    REQUIRE_FALSE(window->IsMinimized());
    SwapchainStatus back = Frame(device.Get(), sc);
    CHECK((back == SwapchainStatus::Ok || back == SwapchainStatus::Recreated));
    CHECK(sc->Width() == window->Width());
    CHECK(Frame(device.Get(), sc) == SwapchainStatus::Ok);
    device->DestroySwapchain(sc);
    device->Quiesce();
}

TEST_CASE("A DX12 device lost at present makes the swapchain report Lost", "[rhi][dx12][gpu][window][swapchain]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    os::PlatformWindowHost window_host;
    REQUIRE(window_host.Open(os::WindowBackend::Win32, "dx12 swapchain loss", 64, 64, &input));
    os::Window* window = window_host.Get();
    DeviceCreateInfo info = Dx12Info(window);
    info.faults.lose_device_at_present = 2;
    Dx12DevicePtr device = Dx12Device::Create(info);
    REQUIRE(device);
    Swapchain* sc = device->CreateSwapchain(SwapchainDesc{});
    REQUIRE(sc != nullptr);
    window->PumpEvents();
    CHECK(Frame(device.Get(), sc) == SwapchainStatus::Ok);
    CHECK(Frame(device.Get(), sc) == SwapchainStatus::Lost);
    CHECK(device->DeviceLost());
    device->BeginFrame();
    CHECK(sc->Acquire() == SwapchainStatus::Lost);
    device->EndFrame();
    device->DestroySwapchain(sc);
}

TEST_CASE("A DX12 swapchain rebuilt inside a frame keeps the frame's destroyed resources until the frame is done",
          "[rhi][dx12][gpu][window][swapchain]")
{
    // Review finding (Day 169): the rebuild waited with WaitIdle, which also freed everything retired, so a buffer
    // destroyed earlier in the frame was gone before the list that uses it was submitted.
    SKIP_IF_NO_DISPLAY_SESSION();
    os::InputState input;
    os::PlatformWindowHost window_host;
    REQUIRE(window_host.Open(os::WindowBackend::Win32, "dx12 swapchain rebuild", 160, 120, &input));
    os::Window* window = window_host.Get();
    SwapchainSilence silence;
    Dx12DevicePtr device = Dx12Device::Create(Dx12Info(window));
    REQUIRE(device);
    Swapchain* sc = device->CreateSwapchain(SwapchainDesc{});
    REQUIRE(sc != nullptr);
    window->PumpEvents();
    REQUIRE(Frame(device.Get(), sc) == SwapchainStatus::Ok);
    BufferDesc bd;
    bd.size = 4096;
    bd.usage = BufferUsage::CopySrc | BufferUsage::CopyDst;
    const BufferHandle a = device->CreateBuffer(bd), b = device->CreateBuffer(bd);
    REQUIRE((a.IsValid() && b.IsValid()));
    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->CopyBuffer(a, b, 0, 0, 4096);
    list->End();
    device->DestroyBuffer(a);  // allowed: the frame keeps it until its work is done
    // The window changes size, so the next Acquire rebuilds the swap chain.
    const HWND hwnd = static_cast<HWND>(window->GetNativeInfo().surface);
    SetWindowPos(hwnd, nullptr, 0, 0, 300, 200, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    window->PumpEvents();
    const SwapchainStatus acquired = sc->Acquire();
    CHECK(acquired == SwapchainStatus::Recreated);
    CommandList* lists[] = {list};
    CHECK(device->Submit(lists, 1) != 0);  // still uses `a`
    if (acquired == SwapchainStatus::Recreated || acquired == SwapchainStatus::Ok)
    {
        CommandList* present = device->CreateCommandList();
        present->Begin();
        present->Barrier(sc->CurrentTexture(), ResourceState::Undefined, ResourceState::Present);
        present->End();
        CommandList* p[] = {present};
        device->Submit(p, 1);
        device->EndFrame();
        sc->Present();
    }
    else
    {
        device->EndFrame();
    }
    device->DestroyBuffer(b);
    device->DestroySwapchain(sc);
    device->Quiesce();
}
