// The Metal swapchain (Phase 26 Day 321): the present-mode mapping, a swapchain over a Cocoa window's CAMetalLayer that
// acquires, clears and presents frames, never keeps a drawable past its frame, reconfigures (present mode, format, HDR
// as the screen allows, size), counts as a live swapchain, is made again after RecreateSurface, and reports Minimized
// for a minimized window. The window tests need the console session unlocked (display group). macOS only.
#include "metal_test_support.h"  // first: it imports AppKit before any engine name is in scope

#include "os/console.h"
#include "os/os.h"

#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "display_session.h"
#include "triangle.frag.dxil.inc"
#include "triangle.vert.dxil.inc"
#include "platform/macos/cocoa_window.h"
#include "rhi/metal/metal_device.h"
#include "rhi/metal/metal_swapchain.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"

#import <AppKit/AppKit.h>

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;
using namespace engine_placeholder::rhi::metal;
using namespace engine_placeholder::rhi::metal::test;
using engine_placeholder::os::cocoa::CocoaWindow;

namespace metal_swapchain_test
{

template <typename Pred> bool PumpUntil(os::Window& window, Pred done, int ms = 3000)
{
    for (int waited = 0; waited < ms; waited += 10)
    {
        window.PumpEvents();
        if (done())
        {
            return true;
        }
        os::SleepNanos(10'000'000);
    }
    return false;
}

// One frame: acquire, clear the image, present. Returns the acquire status.
SwapchainStatus Frame(Device& device, Swapchain& swapchain, f32 red)
{
    device.BeginFrame();
    const SwapchainStatus status = swapchain.Acquire();
    if (status == SwapchainStatus::Ok || status == SwapchainStatus::Recreated)
    {
        CommandList* list = device.CreateCommandList();
        list->Begin();
        RenderingDesc rd;
        rd.color[0].texture = swapchain.CurrentTexture();
        rd.color[0].clear_color[0] = red;
        rd.color_count = 1;
        list->BeginRendering(rd);
        list->EndRendering();
        list->End();
        device.Submit(&list, 1);
        swapchain.Present();
    }
    device.EndFrame();
    return status;
}

}  // namespace metal_swapchain_test

using namespace metal_swapchain_test;

TEST_CASE("Metal present modes: Fifo is display sync, Immediate is none, Mailbox falls back to Fifo", "[rhi][metal]")
{
    CHECK(MetalPresentModeFor(PresentMode::Fifo) == PresentMode::Fifo);
    CHECK(MetalPresentModeFor(PresentMode::Immediate) == PresentMode::Immediate);
    CHECK(MetalPresentModeFor(PresentMode::Mailbox) == PresentMode::Fifo);
}

TEST_CASE("A Metal swapchain presents into a Cocoa window, holds no drawable between frames, and reconfigures",
          "[rhi][metal][gpu][window]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    REQUIRE(window.Open("metal swapchain", 320, 240, nullptr));
    DeviceCreateInfo info = MetalInfo("metal_swapchain_test");
    info.window = &window;
    DevicePtr device = CreateDevice(info);
    REQUIRE(device);
    MetalDevice& metal = AsMetal(*device);
    SwapchainDesc desc;
    Swapchain* swapchain = device->CreateSwapchain(desc);
    REQUIRE(swapchain != nullptr);
    CHECK(device->Resources().swapchains == 1);
    CAMetalLayer* layer = static_cast<MetalSwapchain*>(swapchain)->Layer();
    CHECK(layer.device == metal.Mtl());
    CHECK(layer.pixelFormat == MTLPixelFormatBGRA8Unorm_sRGB);
    CHECK(swapchain->ImageFormat() == Format::BGRA8_SRGB);
    CHECK(swapchain->Width() == window.Width());
    CHECK(swapchain->Height() == window.Height());
    CHECK(static_cast<u32>(layer.drawableSize.width) == window.Width());
    CHECK(swapchain->ActualPresentMode() == PresentMode::Fifo);
    CHECK(layer.displaySyncEnabled);
    CHECK(swapchain->GetColorSpace() == ColorSpace::SrgbNonlinear);
    const SwapchainCaps caps = swapchain->Capabilities();
    CHECK(caps.present_mode_supported[static_cast<usize>(PresentMode::Immediate)]);
    CHECK_FALSE(caps.present_mode_supported[static_cast<usize>(PresentMode::Mailbox)]);
    // What this screen reports for extended dynamic range (recorded in the day file).
    os::Print("metal swapchain: screen EDR potential %.2f, now %.2f\n",
              NSScreen.mainScreen.maximumPotentialExtendedDynamicRangeColorComponentValue,
              NSScreen.mainScreen.maximumExtendedDynamicRangeColorComponentValue);

    for (int f = 0; f < 4; ++f)
    {
        window.PumpEvents();
        const SwapchainStatus s = Frame(*device, *swapchain, 0.25f * static_cast<f32>(f));
        CHECK((s == SwapchainStatus::Ok || s == SwapchainStatus::Recreated));
        CHECK_FALSE(swapchain->CurrentTexture().IsValid());  // given up at Present
    }

    SwapchainDesc immediate;
    immediate.present_mode = PresentMode::Immediate;
    immediate.format = Format::BGRA8_UNorm;
    CHECK(swapchain->Reconfigure(immediate) == SwapchainStatus::Ok);
    CHECK(swapchain->ActualPresentMode() == PresentMode::Immediate);
    CHECK_FALSE(layer.displaySyncEnabled);
    CHECK(layer.pixelFormat == MTLPixelFormatBGRA8Unorm);
    SwapchainDesc mailbox;
    mailbox.present_mode = PresentMode::Mailbox;
    mailbox.format = Format::RGBA8_UNorm;  // not a layer format: bgra8_srgb
    const bool was = LogRhi.Enabled();
    LogRhi.SetEnabled(false);
    CHECK(swapchain->Reconfigure(mailbox) == SwapchainStatus::Ok);
    LogRhi.SetEnabled(was);
    CHECK(swapchain->ActualPresentMode() == PresentMode::Fifo);
    CHECK(swapchain->ImageFormat() == Format::BGRA8_SRGB);
    SwapchainDesc hdr;
    hdr.hdr = HdrMode::ScRgbLinear;
    LogRhi.SetEnabled(false);
    CHECK(swapchain->Reconfigure(hdr) == SwapchainStatus::Ok);
    LogRhi.SetEnabled(was);
    if (caps.hdr_scrgb)
    {
        CHECK(swapchain->ImageFormat() == Format::RGBA16_Float);
        CHECK(swapchain->GetColorSpace() == ColorSpace::ScRgbLinear);
        CHECK(layer.wantsExtendedDynamicRangeContent);
    }
    else
    {
        CHECK(swapchain->ImageFormat() == Format::BGRA8_SRGB);  // this display: no EDR, HDR off
        CHECK(swapchain->GetColorSpace() == ColorSpace::SrgbNonlinear);
    }
    CHECK(swapchain->Resize(200, 100) == SwapchainStatus::Ok);
    CHECK(static_cast<u32>(layer.drawableSize.width) == 200);
    CHECK(static_cast<u32>(layer.drawableSize.height) == 100);
    CHECK(Frame(*device, *swapchain, 1.0f) == SwapchainStatus::Ok);

    // Destroyed and made again after RecreateSurface on the same window.
    device->DestroySwapchain(swapchain);
    CHECK(device->Resources().swapchains == 0);
    REQUIRE(device->RecreateSurface(&window));
    swapchain = device->CreateSwapchain(SwapchainDesc{});
    REQUIRE(swapchain != nullptr);
    CHECK(Frame(*device, *swapchain, 0.5f) != SwapchainStatus::Lost);
    device->DestroySwapchain(swapchain);
    device->WaitIdle();
    CHECK(device->Resources().textures == 0);
}

TEST_CASE("A minimized window's Metal swapchain acquires nothing", "[rhi][metal][gpu][window]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    REQUIRE(window.Open("metal swapchain minimized", 320, 200, nullptr));
    DeviceCreateInfo info = MetalInfo("metal_swapchain_test");
    info.window = &window;
    DevicePtr device = CreateDevice(info);
    REQUIRE(device);
    Swapchain* swapchain = device->CreateSwapchain(SwapchainDesc{});
    REQUIRE(swapchain != nullptr);
    NSView* view = (__bridge NSView*)window.GetNativeInfo().display;
    [view.window miniaturize:nil];
    if (!PumpUntil(window, [&] { return window.IsMinimized(); }))
    {
        device->DestroySwapchain(swapchain);
        SKIP("the Dock did not take the window in 3 s");
    }
    device->BeginFrame();
    CHECK(swapchain->Acquire() == SwapchainStatus::Minimized);
    CHECK_FALSE(swapchain->CurrentTexture().IsValid());
    device->EndFrame();
    [view.window deminiaturize:nil];
    CHECK(PumpUntil(window, [&] { return !window.IsMinimized(); }));
    CHECK(Frame(*device, *swapchain, 1.0f) != SwapchainStatus::Lost);
    device->DestroySwapchain(swapchain);
}

TEST_CASE("After an explicit Resize, a Metal swapchain follows the window again when it is resized",
          "[rhi][metal][gpu][window]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    REQUIRE(window.Open("metal swapchain resize", 320, 240, nullptr));
    DeviceCreateInfo info = MetalInfo("metal_swapchain_test");
    info.window = &window;
    info.faults.lose_device_at_present = 3;
    DevicePtr device = CreateDevice(info);
    REQUIRE(device);
    Swapchain* swapchain = device->CreateSwapchain(SwapchainDesc{});
    REQUIRE(swapchain != nullptr);
    CHECK(swapchain->Resize(200, 100) == SwapchainStatus::Ok);
    CHECK(swapchain->Width() == 200);
    NSView* view = (__bridge NSView*)window.GetNativeInfo().display;
    const u64 serial = window.ResizeSerial();
    [view.window setContentSize:NSMakeSize(400, 300)];
    REQUIRE(PumpUntil(window, [&] { return window.ResizeSerial() != serial; }));
    device->BeginFrame();
    CHECK(swapchain->Acquire() == SwapchainStatus::Recreated);  // once
    CHECK(swapchain->Width() == window.Width());
    CHECK(swapchain->Height() == window.Height());
    CHECK(swapchain->Present() == SwapchainStatus::Ok);  // the first present: not Recreated again
    device->EndFrame();
    CHECK(Frame(*device, *swapchain, 0.5f) == SwapchainStatus::Ok);  // present 2
    // The third present loses the device (the injected fault), and then everything says Lost.
    const bool was = LogRhi.Enabled();
    LogRhi.SetEnabled(false);
    device->BeginFrame();
    REQUIRE(swapchain->Acquire() == SwapchainStatus::Ok);
    CHECK(swapchain->Present() == SwapchainStatus::Lost);
    device->EndFrame();
    LogRhi.SetEnabled(was);
    CHECK(device->DeviceLost());
    CHECK(swapchain->Reconfigure(SwapchainDesc{}) == SwapchainStatus::Lost);
    CHECK(swapchain->Resize(100, 100) == SwapchainStatus::Lost);
    device->DestroySwapchain(swapchain);
}

TEST_CASE("A Metal swapchain image reads back what was rendered to it, byte for byte as an offscreen target",
          "[rhi][metal][gpu][window]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    CocoaWindow window;
    REQUIRE(window.Open("metal swapchain screenshot", 64, 48, nullptr));
    DeviceCreateInfo info = MetalInfo("metal_swapchain_test");
    info.window = &window;
    DevicePtr device = CreateDevice(info);
    REQUIRE(device);
    SwapchainDesc desc;
    desc.format = Format::BGRA8_UNorm;  // no sRGB encoding: the same bytes as the offscreen target
    Swapchain* swapchain = device->CreateSwapchain(desc);
    REQUIRE(swapchain != nullptr);
    // A picture with an orientation: a red triangle in the top-left corner and a green one in the bottom-right, on
    // blue.
    const ShaderHandle vs = MetalShader(*device, shaders::kTriangleVertShader, ShaderStage::Vertex);
    const ShaderHandle fs = MetalShader(*device, shaders::kTriangleFragShader, ShaderStage::Fragment);
    const f32 vertices[6][5] = {{-1, -1, 1, 0, 0}, {0, -1, 1, 0, 0}, {-1, 0, 1, 0, 0},
                                {1, 1, 0, 1, 0},   {0, 1, 0, 1, 0},  {1, 0, 0, 1, 0}};
    BufferDesc bd;
    bd.size = sizeof(vertices);
    bd.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
    const BufferHandle vb = device->CreateBuffer(bd);
    REQUIRE(device->UploadBuffer(vb, 0, vertices, sizeof(vertices)).ok);
    VertexBindingDesc binding{20, false};
    VertexAttributeDesc attributes[2];
    attributes[0].format = VertexFormat::Float2;
    attributes[1].location = 1;
    attributes[1].format = VertexFormat::Float3;
    attributes[1].offset = 8;
    GraphicsPipelineDesc gp;
    gp.vertex_shader = vs;
    gp.fragment_shader = fs;
    gp.vertex_bindings = rhi::Span<VertexBindingDesc>(&binding, 1);
    gp.vertex_attributes = rhi::Span<VertexAttributeDesc>(attributes, 2);
    gp.cull_mode = CullMode::None;
    gp.color_targets[0].format = Format::BGRA8_UNorm;
    gp.color_target_count = 1;
    const PipelineHandle pipeline = device->CreateGraphicsPipeline(gp);
    REQUIRE(pipeline.IsValid());
    TextureDesc td;
    td.width = swapchain->Width();
    td.height = swapchain->Height();
    td.format = Format::BGRA8_UNorm;
    td.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
    const TextureHandle offscreen = device->CreateTexture(td);
    REQUIRE(offscreen.IsValid());
    auto draw = [&](CommandList* list, TextureHandle target)
    {
        RenderingDesc rd;
        rd.color[0].texture = target;
        rd.color[0].clear_color[0] = 0.0f;
        rd.color[0].clear_color[2] = 1.0f;
        rd.color_count = 1;
        list->BeginRendering(rd);
        list->SetPipeline(pipeline);
        list->SetVertexBuffer(0, vb, 0);
        list->Draw(6, 1, 0, 0);
        list->EndRendering();
    };
    device->BeginFrame();
    const SwapchainStatus acquired = swapchain->Acquire();
    REQUIRE((acquired == SwapchainStatus::Ok || acquired == SwapchainStatus::Recreated));
    const TextureHandle image = swapchain->CurrentTexture();
    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->Barrier(image, ResourceState::Undefined, ResourceState::ColorAttachment);  // the contract's barriers
    draw(list, image);
    list->Barrier(offscreen, ResourceState::Undefined, ResourceState::ColorAttachment);
    draw(list, offscreen);
    list->End();
    REQUIRE(device->Submit(&list, 1) != 0);
    const usize bytes = static_cast<usize>(swapchain->Width()) * swapchain->Height() * 4;
    Vector<u8> shown;
    Vector<u8> expected;
    shown.Resize(bytes);
    expected.Resize(bytes);
    REQUIRE(device->ReadbackTexture(image, 0, 0, shown.Data(), bytes, ResourceState::ColorAttachment));
    REQUIRE(device->ReadbackTexture(offscreen, 0, 0, expected.Data(), bytes, ResourceState::ColorAttachment));
    CHECK(MemCompare(shown.Data(), expected.Data(), bytes) == 0);
    CHECK(shown[2] == 255);          // the top-left pixel is red (BGRA: R at byte 2): rows are top first
    CHECK(shown[bytes - 3] == 255);  // the bottom-right one green
    CommandList* present = device->CreateCommandList();
    present->Begin();
    present->Barrier(image, ResourceState::ColorAttachment, ResourceState::Present);
    present->End();
    REQUIRE(device->Submit(&present, 1) != 0);
    CHECK(swapchain->Present() == SwapchainStatus::Ok);
    // After Present there is no drawable (the system took it back): a read is refused, not a crash.
    const bool was = LogRhi.Enabled();
    LogRhi.SetEnabled(false);
    CHECK_FALSE(device->ReadbackTexture(image, 0, 0, shown.Data(), bytes, ResourceState::Present));
    LogRhi.SetEnabled(was);
    device->EndFrame();
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vs);
    device->DestroyShader(fs);
    device->DestroyBuffer(vb);
    device->DestroyTexture(offscreen);
    device->DestroySwapchain(swapchain);
}
