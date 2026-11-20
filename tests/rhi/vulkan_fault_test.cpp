#include "os/console.h"

#include <catch2/catch_test_macros.hpp>
#include "display_session.h"

#include "gpu_fixture.h"
#include "platform/input.h"
#include "platform/window_host.h"
#include "rhi/rhi.h"
#include "rhi/vulkan/vulkan_device.h"
#include "build_defs.h"

using namespace vse;
using namespace vse::rhi;

// Failure injection (Phase 4 Day 66): the error paths real hardware rarely reaches. Every test here ends with the
// device destroyed and the validation layer silent -- an injected fault must never turn into a leak or a crash.

namespace
{

DeviceCreateInfo Info(const FaultInjection& faults)
{
    DeviceCreateInfo info;
    info.backend = Backend::Vulkan;
    info.validation = ValidationSetting::On;
    info.faults = faults;
    return info;
}

// The tests may look behind the interface: the Vulkan device counts its live resources.
vulkan::VulkanDevice* Vk(Device* device)
{
    return static_cast<vulkan::VulkanDevice*>(device);
}

bool HaveEnv(const char* name)
{
    const char* v = os::GetEnvironment(name);
    return v != nullptr && v[0] != '\0';
}

}  // namespace

TEST_CASE("A device lost at a chosen frame ignores submissions and tears down cleanly", "[rhi][vulkan][gpu][fault]")
{
    gpu_test::GpuFixture fx;
    fx.Require();
    FaultInjection faults;
    faults.lose_device_at_frame = 3;
    DevicePtr device = CreateDevice(Info(faults));
    if (device == nullptr)
    {
        SKIP("no suitable Vulkan device");
    }
    BufferDesc bd;
    bd.size = 256;
    bd.usage = BufferUsage::Uniform;
    bd.location = MemoryLocation::CpuToGpu;
    const BufferHandle buffer = device->CreateBuffer(bd);
    REQUIRE(buffer.IsValid());

    for (u32 frame = 1; frame <= 2; ++frame)
    {
        device->BeginFrame();
        CHECK_FALSE(device->DeviceLost());
        CommandList* list = device->CreateCommandList();
        list->Begin();
        list->End();
        CommandList* lists[] = {list};
        CHECK(device->Submit(lists, 1) != 0);
        device->EndFrame();
    }
    device->BeginFrame();  // frame 3: the fault fires
    CHECK(device->DeviceLost());
    CommandList* list = device->CreateCommandList();
    if (list != nullptr)
    {
        list->Begin();
        list->End();
        CommandList* lists[] = {list};
        CHECK(device->Submit(lists, 1) == 0);  // ignored, not a crash
    }
    device->EndFrame();
    device->BeginFrame();  // frames keep working after the loss, and resources can still be destroyed
    device->DestroyBuffer(buffer);
    device->EndFrame();
    device->WaitIdle();
    CHECK(Vk(device.Get())->Counts().buffers == 0);
    device.Reset();
}

TEST_CASE("Device creation fails cleanly when the first allocation fails", "[rhi][vulkan][gpu][fault]")
{
    gpu_test::GpuFixture fx;
    fx.Require();
    FaultInjection faults;
    faults.fail_allocations_from = 1;
    CHECK(CreateDevice(Info(faults)) == nullptr);  // the staging ring cannot be allocated; nothing leaks
}

TEST_CASE("Out-of-memory returns invalid handles and the device keeps working", "[rhi][vulkan][gpu][fault]")
{
    gpu_test::GpuFixture fx;
    fx.Require();
    // Find how many allocations device creation itself makes, then fail from the next one on.
    // Creation makes a handful (the staging ring); the first n for which it succeeds is one past that count.
    u32 creation_allocations = 0;
    bool found = false;
    for (u32 n = 1; n <= 16 && !found; ++n)
    {
        FaultInjection faults;
        faults.fail_allocations_from = n;
        DevicePtr probe = CreateDevice(Info(faults));
        if (probe != nullptr)
        {
            found = true;
            creation_allocations = n - 1;
            probe.Reset();
        }
    }
    REQUIRE(found);
    REQUIRE(creation_allocations > 0);  // creation does allocate: the staging ring is mandatory

    FaultInjection faults;
    faults.fail_allocations_from = creation_allocations + 1;
    DevicePtr device = CreateDevice(Info(faults));
    REQUIRE(device != nullptr);

    TextureDesc td;
    td.width = 64;
    td.height = 64;
    td.format = Format::RGBA8_UNorm;
    td.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
    CHECK_FALSE(device->CreateTexture(td).IsValid());  // dedicated allocation for a render target fails
    BufferDesc bd;
    bd.size = 1 << 20;
    bd.usage = BufferUsage::Vertex;
    bd.location = MemoryLocation::GpuOnly;
    CHECK_FALSE(device->CreateBuffer(bd).IsValid());

    // A frame still runs, and nothing was left half-created.
    device->BeginFrame();
    CommandList* list = device->CreateCommandList();
    list->Begin();
    list->End();
    CommandList* lists[] = {list};
    CHECK(device->Submit(lists, 1) != 0);
    device->EndFrame();
    device->WaitIdle();
    CHECK(Vk(device.Get())->Counts().textures == 0);
    CHECK(Vk(device.Get())->Counts().buffers == 0);
    CHECK_FALSE(device->DeviceLost());
    device.Reset();
}

namespace
{

void RunResizeStorm(os::WindowBackend backend)
{
    os::InputState input;
    os::PlatformWindowHost host;
    if (!host.Open(backend, "rhi resize storm", 320, 240, &input))
    {
        SKIP("the window could not be opened");
    }
    os::Window* window = host.Get();
    REQUIRE(window != nullptr);
    gpu_test::GpuFixture fx(/*presentable=*/true);
    fx.Require();
    DeviceCreateInfo info = Info(FaultInjection{});
    info.window = window;
    DevicePtr device = CreateDevice(info);
    if (device == nullptr)
    {
        if (gpu_test::GpuRequired())
        {
            FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no Vulkan device could present to the window");
        }
        SKIP("no Vulkan device can present to this window");
    }
    SwapchainDesc desc;
    desc.present_mode = PresentMode::Mailbox;  // falls back to FIFO where the system lacks it; either must present
    Swapchain* sc = device->CreateSwapchain(desc);
    REQUIRE(sc != nullptr);

    u32 seed = 12345;
    auto next = [&seed]()
    {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 16;
    };
    for (int i = 0; i < 200; ++i)
    {
        window->PumpEvents();
        // Mostly sane sizes, some degenerate ones: zero in one or both dimensions, 1x1, and a large one.
        u32 w = 1 + next() % 600;
        u32 h = 1 + next() % 400;
        switch (i % 10)
        {
        case 3: w = 0; break;
        case 5: h = 0; break;
        case 7: w = h = 0; break;
        case 8: w = h = 1; break;
        case 9:
            w = 2000;
            h = 1500;
            break;
        default: break;
        }
        const SwapchainStatus resized = sc->Resize(w, h);
        CHECK((resized == SwapchainStatus::Ok || resized == SwapchainStatus::Minimized ||
               resized == SwapchainStatus::Recreated));
        if (i % 4 == 0 && resized != SwapchainStatus::Minimized)
        {
            device->BeginFrame();
            const SwapchainStatus acquired = sc->Acquire();
            if (acquired == SwapchainStatus::Ok || acquired == SwapchainStatus::Recreated)
            {
                CommandList* list = device->CreateCommandList();
                list->Begin();
                list->Barrier(sc->CurrentTexture(), ResourceState::Undefined, ResourceState::Present);
                list->End();
                CommandList* lists[] = {list};
                CHECK(device->Submit(lists, 1) != 0);
                device->EndFrame();
                sc->Present();
            }
            else
            {
                device->EndFrame();
            }
        }
    }
    device->WaitIdle();
    fx.ExpectErrors(0);
    device->DestroySwapchain(sc);
    device->WaitIdle();
    CHECK(Vk(device.Get())->Counts().textures == 0);
    device.Reset();
}

}  // namespace

TEST_CASE("A swapchain survives 200 resize requests including zero sizes on Wayland",
          "[rhi][vulkan][gpu][window][wayland][fault]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    if (!HaveEnv("WAYLAND_DISPLAY"))
    {
        SKIP("no Wayland session");
    }
    RunResizeStorm(os::WindowBackend::Wayland);
}

TEST_CASE("A swapchain survives 200 resize requests including zero sizes on X11",
          "[rhi][vulkan][gpu][window][x11][fault]")
{
    SKIP_IF_NO_DISPLAY_SESSION();
    if (!HaveEnv("DISPLAY"))
    {
        SKIP("no X11 display");
    }
    RunResizeStorm(os::WindowBackend::X11);
}

// Phase 13 Day 160: the same checks through the Win32 window and the VK_KHR_win32_surface surface.
TEST_CASE("A swapchain survives 200 resize requests including zero sizes on Win32",
          "[rhi][vulkan][gpu][window][win32][fault]")
{
#if ENGINE_PLATFORM_WINDOWS
    SKIP_IF_NO_DISPLAY_SESSION();
    RunResizeStorm(os::WindowBackend::Win32);
#else
    SKIP("Win32 is the Windows backend");
#endif
}
