// Phase 8 Day 114: the lifecycle conformance suite. Every backend must pass it; a new backend (DX12, Metal) runs it by
// adding one factory to the list at the bottom. See engine/rhi/BACKEND-LIFECYCLE.md.
#include "cstr/cstr.h"

#include <catch2/catch_test_macros.hpp>

#include "build_defs.h"
#include "gpu_fixture.h"
#include "rhi/device_host.h"
#include "rhi/rhi.h"
#include "rhi/rhi_log.h"

using namespace engine_placeholder;
using namespace engine_placeholder::rhi;

namespace
{

struct ConformanceQuietLog
{
    bool saved = LogRhi.Enabled();
    ConformanceQuietLog() { LogRhi.SetEnabled(false); }
    ~ConformanceQuietLog() { LogRhi.SetEnabled(saved); }
};

// What a backend supplies: a description for its devices and whether a headless device can have a swapchain.
struct Backend_
{
    const char* name;
    DeviceCreateInfo info;
    bool headless_swapchain;  // the null backend can; Vulkan needs a window
};

BufferDesc Uniform64()
{
    BufferDesc d;
    d.size = 64;
    d.usage = BufferUsage::Uniform;
    d.location = MemoryLocation::CpuToGpu;
    return d;
}

void RunLifecycleConformance(const Backend_& backend)
{
    INFO("backend " << backend.name);
    // Epoch, adapter and enumeration.
    AdapterList list;
    REQUIRE(EnumerateAdapters(backend.info, &list));
    REQUIRE(list.count >= 1);
    AdapterList again;
    REQUIRE(EnumerateAdapters(backend.info, &again));
    REQUIRE(again.count == list.count);
    for (u32 i = 0; i < list.count; ++i)
    {
        CHECK(CStrCompare(list.items[i].id, again.items[i].id) == 0);
    }

    DevicePtr a = CreateDevice(backend.info);
    REQUIRE(a != nullptr);
    DevicePtr b = CreateDevice(backend.info);
    REQUIRE(b != nullptr);
    CHECK(a->Epoch() != 0);
    CHECK(a->Epoch() != b->Epoch());
    bool adapter_listed = false;
    for (u32 i = 0; i < list.count; ++i)
    {
        adapter_listed = adapter_listed || CStrCompare(a->Adapter().id, list.items[i].id) == 0;
    }
    CHECK(adapter_listed);

    // Handles of one device are not the handles of the next: the top bits of the generation differ.
    const BufferHandle ha = a->CreateBuffer(Uniform64());
    const BufferHandle hb = b->CreateBuffer(Uniform64());
    REQUIRE(ha.IsValid());
    REQUIRE(hb.IsValid());
    CHECK(ha != hb);
    CHECK((ha.generation >> 20) != (hb.generation >> 20));

    // Counts follow creation and destruction, exactly after Quiesce.
    CHECK(a->Resources().buffers == 1);
    a->DestroyBuffer(ha);
    a->Quiesce();
    CHECK(a->Resources().UserObjects() == 0);
    CHECK(a->Resources().buffers == 0);

    // Quiesce keeps the device usable, and InFrame follows BeginFrame/EndFrame.
    CHECK_FALSE(a->InFrame());
    a->BeginFrame();
    CHECK(a->InFrame());
    a->EndFrame();
    CHECK_FALSE(a->InFrame());
    a->Quiesce();
    a->BeginFrame();
    a->EndFrame();
    CHECK_FALSE(a->DeviceLost());

    // A device told it is lost: sticky, waits return, destruction works.
    const BufferHandle held = b->CreateBuffer(Uniform64());
    REQUIRE(held.IsValid());
    b->InjectDeviceLoss();
    CHECK(b->DeviceLost());
    b->Quiesce();
    b->WaitIdle();
    b->DestroyBuffer(held);
    b->DestroyBuffer(hb);
    b->Quiesce();
    CHECK(b->DeviceLost());  // once lost, always lost
    b.Reset();
    a.Reset();

    // The host replaces devices: counts return to the same value every time and handles never carry over.
    DeviceHost host;
    REQUIRE(host.Create(backend.info, SwapchainDesc{}));
    u32 last = host.Epoch();
    for (int i = 0; i < 5; ++i)
    {
        REQUIRE(host.Recreate(backend.info, SwapchainDesc{}));
        CHECK(host.Epoch() != last);
        last = host.Epoch();
        CHECK(host.GetDevice()->Resources().UserObjects() == 0);
    }
    CHECK(host.LeakedObjects() == 0);
    host.Destroy();

    // Swapchain reconfiguration where the backend can make one without a window.
    if (backend.headless_swapchain)
    {
        DevicePtr d = CreateDevice(backend.info);
        SwapchainDesc desc;
        desc.width = 64;
        desc.height = 48;
        Swapchain* sc = d->CreateSwapchain(desc);
        REQUIRE(sc != nullptr);
        desc.width = 80;
        desc.height = 60;
        desc.present_mode = PresentMode::Immediate;
        CHECK(sc->Reconfigure(desc) == SwapchainStatus::Ok);
        CHECK(sc->Width() == 80);
        const PresentMode actual = sc->ActualPresentMode();
        CHECK((actual == PresentMode::Immediate ||
               actual == PresentMode::Fifo));                 // a fallback is allowed, reported truthfully
        CHECK(sc->Capabilities().present_mode_supported[0]);  // Fifo is always available
        CHECK(sc->GetColorSpace() == ColorSpace::SrgbNonlinear);
        CHECK(sc->Resize(0, 0) == SwapchainStatus::Minimized);
        CHECK(sc->Resize(40, 30) == SwapchainStatus::Ok);
        d->DestroySwapchain(sc);
        (void)d->RecreateSurface(nullptr);  // backends differ on a null window; the call must not crash
        d->Quiesce();
        CHECK(d->Resources().UserObjects() == 0);
    }
}

}  // namespace

TEST_CASE("The null backend conforms to the device lifecycle", "[rhi][lifecycle][conformance]")
{
    ConformanceQuietLog quiet;
    Backend_ null_backend;
    null_backend.name = "null";
    null_backend.info.backend = Backend::Null;
    null_backend.headless_swapchain = true;
    RunLifecycleConformance(null_backend);
}

TEST_CASE("The Vulkan backend conforms to the device lifecycle", "[rhi][lifecycle][conformance][vulkan][gpu]")
{
    gpu_test::GpuFixture fx;
    fx.Require();
    ConformanceQuietLog quiet;
    Backend_ vulkan_backend;
    vulkan_backend.name = "vulkan";
    vulkan_backend.info.backend = Backend::Vulkan;
    vulkan_backend.info.validation = ValidationSetting::Off;
    vulkan_backend.headless_swapchain = false;
    DevicePtr probe = CreateDevice(vulkan_backend.info);
    if (probe == nullptr)
    {
        SKIP("no suitable Vulkan device");
    }
    probe.Reset();
    RunLifecycleConformance(vulkan_backend);
}

// Phase 26 Day 325: Metal on the Mac's adapter (validation is the process's: the macOS ctests set MTL_DEBUG_LAYER).
TEST_CASE("The Metal backend conforms to the device lifecycle", "[rhi][lifecycle][conformance][metal][gpu]")
{
#if ENGINE_PLATFORM_MACOS
    ConformanceQuietLog quiet;
    Backend_ metal_backend;
    metal_backend.name = "metal";
    metal_backend.info.backend = Backend::Metal;
    metal_backend.info.validation = ValidationSetting::Off;
    metal_backend.headless_swapchain = false;  // a CAMetalLayer swapchain needs a window
    DevicePtr probe = CreateDevice(metal_backend.info);
    if (probe == nullptr)
    {
        if (gpu_test::GpuRequired())
        {
            FAIL("ENGINE_RHI_REQUIRE_GPU=1 but no Metal device could be created");
        }
        SKIP("no Metal device (a Mac without a Metal 3 GPU)");
    }
    probe.Reset();
    RunLifecycleConformance(metal_backend);
#else
    SKIP("Metal is the macOS backend");
#endif
}

// Phase 13 Day 172: Direct3D 12 on the first hardware adapter, and on WARP (a second implementation, and a GPU-free
// check).
TEST_CASE("The DX12 backend conforms to the device lifecycle, on hardware and on WARP",
          "[rhi][lifecycle][conformance][dx12][gpu]")
{
#if ENGINE_PLATFORM_WINDOWS
    ConformanceQuietLog quiet;
    for (const char* adapter : {"0", "Basic Render"})
    {
        DYNAMIC_SECTION("adapter " << adapter)
        {
            Backend_ dx12_backend;
            dx12_backend.name = "dx12";
            dx12_backend.info.backend = Backend::Dx12;
            dx12_backend.info.validation = ValidationSetting::Off;
            dx12_backend.info.adapter = adapter;
            dx12_backend.headless_swapchain = false;  // a DXGI swap chain needs a window
            DevicePtr probe = CreateDevice(dx12_backend.info);
            REQUIRE(probe != nullptr);  // every Windows 10 and 11 has Direct3D 12 and WARP
            probe.Reset();
            RunLifecycleConformance(dx12_backend);
        }
    }
#else
    SKIP("Direct3D 12 is the Windows backend");
#endif
}
