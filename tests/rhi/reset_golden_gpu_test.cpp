// Phase 8 Day 112: the reset on a real device: the picture before and after a reset is identical and still matches the
// blessed golden image, and a loop of resets leaves nothing behind.

#include <catch2/catch_test_macros.hpp>

#include "backend_fixture.h"
#include "log/output.h"
#include "gpu_fixture.h"
#include "image_compare.h"
#include "os/console.h"
#include "os/os.h"
#include "reset_rig.h"
#include "rhi/rhi_log.h"
#include "soak.h"
#include "build_defs.h"
#include "cstr/cstr.h"
#include "sync/atomic.h"

#if ENGINE_PLATFORM_MACOS
#include <pthread.h>  // pthread_self: the thread the Objective-C runtime names in its missing-pool reports
#endif

using namespace vse;
using namespace vse::renderer;

namespace
{

constexpr u32 kResetShotW = 320;
constexpr u32 kResetShotH = 180;

// One return per configuration: code after an unconditional return is a warning (C4702) under /WX.
bool Sanitized()
{
#if ENGINE_SANITIZED
    return true;
#else
    return false;
#endif
}


// The Direct3D 12 debug layer's GPU-based validation, on the Windows runner's WARP (10.0.26100.33648), reports `Incompatible
// texture barrier layout` for a mip-generation dispatch in about one full run of three, with the barriers valid and the
// failing level and texture moving from run to run; it never shows on the PC's WARP (10.0.26100.10068) or on a hardware
// adapter (KNOWN-ISSUES.md, CI run 9 and the investigation after it). The reset tests count those messages and, on a
// software adapter only, expect them: any other validation error, and the same message on a hardware adapter, still fails.
class SoftwareMipLayoutNoise
{
public:
    SoftwareMipLayoutNoise()
    {
#if ENGINE_PLATFORM_WINDOWS
        rhi::dx12::SetDebugMessageCapture(&Capture, this);
#endif
    }
    ~SoftwareMipLayoutNoise()
    {
#if ENGINE_PLATFORM_WINDOWS
        rhi::dx12::SetDebugMessageCapture(nullptr, nullptr);
#endif
    }
    SoftwareMipLayoutNoise(const SoftwareMipLayoutNoise&) = delete;
    SoftwareMipLayoutNoise& operator=(const SoftwareMipLayoutNoise&) = delete;

    // The messages seen so far.
    u64 Count() const { return count_.Load(MemoryOrder::Relaxed); }

    // Declares them expected on `validation` when the device is a software one; says so in the test's output.
    void ExpectOnSoftwareDevice(rhi::Device* device, gpu_test::BackendValidation* validation) const
    {
        const u64 seen = Count();
        if (seen == 0 || device == nullptr || device->Adapter().kind != rhi::AdapterKind::Software)
        {
            return;
        }
        WARN(seen << " GPU-based validation layout message(s) on a software adapter were not counted (known: the "
                     "validator's, not the engine's)");
        validation->ExpectErrors(seen);
    }

private:
    static void Capture(void* user, LogVerbosity severity, u32, const char* text)
    {
        if (severity == LogVerbosity::Error && CStrFind(text, "GPU-BASED VALIDATION") != nullptr &&
            CStrFind(text, "Incompatible texture barrier layout") != nullptr)
        {
            static_cast<SoftwareMipLayoutNoise*>(user)->count_.FetchAdd(1, MemoryOrder::Relaxed);
        }
    }

    Atomic<u64> count_{0};
};
}  // namespace

TEST_CASE("The demo picture is identical before and after a device reset and still matches the golden",
          "[rhi][gpu][golden][reset]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        reset_rig::Rig rig;
        rig.target.width = kResetShotW;
        rig.target.height = kResetShotH;
        if (!rig.Create(backend, 540, 300, false, nullptr, ENGINE_RENDERER_DEMO_ASSET_DIR, /*validation=*/true))
        {
            rig.TearDown();
            SKIP("no suitable " << gpu_test::BackendTag(backend) << " device");
        }
        auto shoot = [&](gpu_test::Image* image)
        {
            REQUIRE(rig.Render() == FrameStatus::Ok);
            REQUIRE(rig.frame.LastGBufferOk());
            REQUIRE(rig.Read(image));
        };
        gpu_test::Image before;
        gpu_test::Image after;
        shoot(&before);

        // A reset with the same adapter and driver: zero tolerance, the two pictures are the same bytes.
        const ApplyResult result = rig.ResetDevice(
            false);  // sync validation was on: this is a real change, so a device reset
        REQUIRE(result.ok);
        CHECK(result.performed == CostClass::DeviceReset);
        shoot(&after);
        CHECK(before.rgba == after.rgba);

        // And both match the blessed golden within its existing tolerance.
        gpu_test::Image golden;
        REQUIRE(gpu_test::ReadPpm(gpu_test::GoldenPath("renderer_demo.ppm").CStr(), &golden));
        gpu_test::CompareOptions co;
        co.tolerance = 6;
        co.edge_tolerance = 90;
        co.max_edge_miss = 0.3;
        co.max_interior_miss = 0.002;
        const gpu_test::CompareResult against_before = gpu_test::Compare(golden, before, co, "reset_before");
        const gpu_test::CompareResult against_after = gpu_test::Compare(golden, after, co, "reset_after");
        INFO(against_before.message.CStr());
        CHECK(against_before.same);
        INFO(against_after.message.CStr());
        CHECK(against_after.same);

        rig.TearDown();
    }
}

TEST_CASE("The demo picture 100,000 km from the world origin equals the picture at the origin",
          "[rhi][gpu][golden][large-world]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        gpu_test::Image images[2];
        bool made = true;
        for (int far = 0; far < 2 && made; ++far)
        {
            reset_rig::Rig rig;
            rig.target.width = kResetShotW;
            rig.target.height = kResetShotH;
            // Objects, lights and the camera all stand 1e8 + 0.25 m away on every axis; the render origin sits there
            // too, so only the double precision positions differ from the near scene.
            const f64 offset = far != 0 ? 1.0e8 + 0.25 : 0.0;
            rig.origin_offset = Vec3d(offset, offset, offset);
            made = rig.Create(backend, 540, 300, false, nullptr, ENGINE_RENDERER_DEMO_ASSET_DIR, /*validation=*/true);
            if (made)
            {
                REQUIRE(rig.Render() == FrameStatus::Ok);
                REQUIRE(rig.Read(&images[far]));
            }
            rig.TearDown();
        }
        if (!made)
        {
            SKIP("no suitable " << gpu_test::BackendTag(backend) << " device");
        }
        gpu_test::CompareOptions co;
        co.tolerance = 2;
        co.edge_tolerance = 32;
        co.max_edge_miss = 0.05;
        co.max_interior_miss = 0.0005;
        const gpu_test::CompareResult result = gpu_test::Compare(images[0], images[1], co, "large_world_far");
        INFO(result.message.CStr());
        CHECK(result.same);
    }
}

TEST_CASE("Resets in a loop on a real device leave the counts, memory and descriptors where they were",
          "[rhi][gpu][reset]")
{
#if ENGINE_PLATFORM_MACOS
    // For the ctest metal_autorelease_pools (tests/rhi/macos/run_autorelease_pools.cmake): the reports it fails on are
    // this thread's, the engine's calls; Metal's own threads release what it held and report too (Day 309).
    const char* print_thread = os::GetEnvironment("ENGINE_RHI_PRINT_TEST_THREAD");
    if (print_thread != nullptr && print_thread[0] == '1')
    {
        out::Print("test thread (%p)\n", reinterpret_cast<const void*>(::pthread_self()));
    }
#endif
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        SoftwareMipLayoutNoise noise;
        reset_rig::Rig rig;
        if (!rig.Create(backend, 64, 8, false, nullptr, nullptr, /*validation=*/true))
        {
            rig.TearDown();
            SKIP("no suitable " << gpu_test::BackendTag(backend) << " device");
        }
        REQUIRE(rig.Render() == FrameStatus::Ok);
        rig.host.GetDevice()->Quiesce();
        rhi::ResourceCounts first = rig.host.GetDevice()->Resources();
        const usize owners = rig.registry.Count();
        // 20 resets in the ordinary run (and always under a sanitizer); the soak form ("... [soak]",
        // ENGINE_TEST_SOAK=1) runs 100. Every reset compares the device's object counts and the registry, so a
        // per-reset leak of an object fails at the second reset either way; the memory drift between resets 5-9 and the
        // last five is what the longer run sees more finely.
        const int iterations = Sanitized() ? 20 : static_cast<int>(test_support::SoakCount(20, 100));
        // Warm the loop up before the baseline: driver caches and allocator pools settle in the first iterations.
        // Memory is compared by its lowest reading over resets 5 to 9 and over the last five: a leak per reset raises
        // the low point. On Windows (AMD Radeon 860M, Day 161) the process holds about 130 MiB more after some resets
        // and gives it back a few resets later, while the device's own memory (checked by ExpectSameCounts) does not
        // change, so one reading can land on a step.
        i64 early_low = 0;
        i64 late_low = 0;
        i64 early_handles_low = 0;
        i64 late_handles_low = 0;
        for (int i = 0; i < iterations; ++i)
        {
            INFO("reset " << i);
            REQUIRE(rig.ResetDevice(i % 2 != 0).ok);  // alternates; the first call turns sync validation off
            REQUIRE(rig.Render() == FrameStatus::Ok);
            rig.host.GetDevice()->Quiesce();
            const rhi::ResourceCounts now = rig.host.GetDevice()->Resources();
            if (i == 0)
            {
                first = now;  // the first frame after a reset caches its frame and object bind groups; every later
                              // reset must match this
            }
            reset_rig::ExpectSameCounts(now, first);
            CHECK(rig.registry.Count() == owners);
            if ((i >= 4 && i < 9) || i >= iterations - 5)
            {
                const i64 resident = os::ResidentMemoryKiB();
                i64& low = i < 9 ? early_low : late_low;
                low = (low == 0 || resident < low) ? resident : low;
                // The same for the open handles: a device keeps a few (a pool's thread, an event) for a reset or two and
                // gives them back, so one reading can land on a step (CI run 8, WARP: +8 after 20 resets, -4 after 100).
                const i64 handles = os::OpenHandleCount();
                i64& handles_low = i < 9 ? early_handles_low : late_handles_low;
                handles_low = (handles_low == 0 || handles < handles_low) ? handles : handles_low;
            }
        }
        const i64 drift_kib = late_low - early_low;
        const i64 handle_drift = late_handles_low - early_handles_low;
        WARN("after " << iterations << " resets: lowest resident " << early_low << " KiB over resets 5-9, " << late_low
                      << " KiB over the last five (drift " << drift_kib << " KiB), handle drift " << handle_drift
                      << ", last reset " << rig.R().Stats().last_reset_ms << " ms");
        // A leak per reset raises the low point by about one handle per reset. Direct3D 12's drift over 100 resets ranged
        // from -1 to 3 on the Radeon 860M (Day 172, five runs, one of them beside other tests; one reading against one),
        // Vulkan's stays at 0. The low points over resets 5-9 and the last five are compared, as memory is: on WARP a
        // reading lands on a transient step of two or three handles.
        CHECK(handle_drift <= (backend == rhi::Backend::Dx12 ? 4 : 2));
        // Metal's validation layer in assert mode grows the process by 8 to 13 MiB over the 100 resets (Phase 21 Day
        // 325, four runs), in its own completion handlers on Metal's thread; without it the drift is 1.4 to 3.4 MiB.
        // The ctest metal_autorelease_pools runs this test without validation, so the check still holds for the engine.
        const char* metal_layer = os::GetEnvironment("MTL_DEBUG_LAYER");
        const bool metal_validation = backend == rhi::Backend::Metal && metal_layer != nullptr && metal_layer[0] == '1';
        if (metal_validation)
        {
            WARN("memory drift not checked: Metal's validation layer is on (see metal_autorelease_pools)");
        }
        // A software device (llvmpipe) keeps its render targets and caches in the process: the drift is its
        // allocator's, not a leak (the object counts and the registry, checked at every reset, still hold).
        const bool software = rig.host.GetDevice()->Adapter().kind == rhi::AdapterKind::Software;
        if (software)
        {
            WARN("memory drift not checked: the device is a software implementation");
        }
        if (!Sanitized() && !metal_validation && !software && early_low != 0 && late_low != 0)
        {
            i64 allowed_kib = 8 * 1024;
#if ENGINE_PLATFORM_WINDOWS
            allowed_kib += 4 * 64 * 1024;  // the old devices' memory Windows still counts in the process: up to four 64
                                           // MiB blocks (Day 161)
#endif
            CHECK(drift_kib < allowed_kib);
        }
        noise.ExpectOnSoftwareDevice(rig.host.GetDevice(), &validation);
        CHECK(rig.R().Stats().resets == static_cast<u64>(iterations));
        CHECK(rig.R().Stats().rollbacks == 0);
        CHECK(rig.host.LeakedObjects() == 0);
        rig.TearDown();
        CHECK(rig.host.LeakedObjects() == 0);
    }
}

// The textures a reset recreates generate their mips again (GpuTexture: level 0 uploaded, then GenerateMips on a list of
// the device's own). With GPU-based validation on (every other reset here), the validator must see each level in the
// layout the dispatch needs. CI run 8 (WARP, the 100-reset soak form) reported `Incompatible texture barrier layout:
// 'normal.rtex', Layout: D3D12_BARRIER_LAYOUT_COMMON` for the mip dispatches; this is the short form of that loop, so a
// change to the mip barriers is checked without the soak.
TEST_CASE("Resets that recreate mipmapped textures generate their mips under GPU-based validation clean",
          "[rhi][gpu][reset]")
{
    FOR_EACH_GPU_BACKEND(backend)
    {
        gpu_test::BackendValidation validation(backend);
        SoftwareMipLayoutNoise noise;
        reset_rig::Rig rig;
        if (!rig.Create(backend, 64, 8, false, nullptr, nullptr, /*validation=*/true))
        {
            rig.TearDown();
            SKIP("no suitable " << gpu_test::BackendTag(backend) << " device");
        }
        REQUIRE(rig.Render() == FrameStatus::Ok);
        const int kResets = Sanitized() ? 4 : 10;
        for (int i = 0; i < kResets; ++i)
        {
            INFO("reset " << i);
            REQUIRE(rig.ResetDevice(i % 2 != 0).ok);  // GPU-based validation is on after the odd resets
            REQUIRE(rig.Render() == FrameStatus::Ok);
            rig.host.GetDevice()->Quiesce();
        }
        noise.ExpectOnSoftwareDevice(rig.host.GetDevice(), &validation);
        rig.TearDown();
    }
}
