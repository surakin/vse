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
        i64 baseline_handles = 0;
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
            if (i == 4)
            {
                baseline_handles = os::OpenHandleCount();
            }
            if ((i >= 4 && i < 9) || i >= iterations - 5)
            {
                const i64 resident = os::ResidentMemoryKiB();
                i64& low = i < 9 ? early_low : late_low;
                low = (low == 0 || resident < low) ? resident : low;
            }
        }
        const i64 drift_kib = late_low - early_low;
        const i64 handle_drift = os::OpenHandleCount() - baseline_handles;
        WARN("after " << iterations << " resets: lowest resident " << early_low << " KiB over resets 5-9, " << late_low
                      << " KiB over the last five (drift " << drift_kib << " KiB), handle drift " << handle_drift
                      << ", last reset " << rig.R().Stats().last_reset_ms << " ms");
        // A leak per reset would show about one handle per reset. Direct3D 12's drift over 100 resets ranged from -1 to
        // 3 on the Radeon 860M (Day 172, five runs, one of them beside other tests), Vulkan's stays at 0.
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
        if (!Sanitized() && !metal_validation && early_low != 0 && late_low != 0)
        {
            i64 allowed_kib = 8 * 1024;
#if ENGINE_PLATFORM_WINDOWS
            allowed_kib += 4 * 64 * 1024;  // the old devices' memory Windows still counts in the process: up to four 64
                                           // MiB blocks (Day 161)
#endif
            CHECK(drift_kib < allowed_kib);
        }
        CHECK(rig.R().Stats().resets == static_cast<u64>(iterations));
        CHECK(rig.R().Stats().rollbacks == 0);
        CHECK(rig.host.LeakedObjects() == 0);
        rig.TearDown();
        CHECK(rig.host.LeakedObjects() == 0);
    }
}
