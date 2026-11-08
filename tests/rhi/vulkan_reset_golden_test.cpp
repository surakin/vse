// Phase 8 Day 112: the reset on a real device: the picture before and after a reset is identical and still matches the blessed
// golden image, and a loop of resets leaves nothing behind.

#include <cstdio>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gpu_fixture.h"
#include "image_compare.h"
#include "os/os.h"
#include "reset_rig.h"
#include "rhi/rhi_log.h"
#include "build_defs.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace {

constexpr u32 kResetShotW = 320;
constexpr u32 kResetShotH = 180;

// One return per configuration: code after an unconditional return is a warning (C4702) under /WX.
bool Sanitized() {
#if ENGINE_SANITIZED
    return true;
#else
    return false;
#endif
}

} // namespace

TEST_CASE("The demo picture is identical before and after a device reset and still matches the golden", "[rhi][vulkan][gpu][golden][reset]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    reset_rig::Rig rig;
    rig.target.width = kResetShotW;
    rig.target.height = kResetShotH;
    if (!rig.Create(rhi::Backend::Vulkan, 540, 300, false, nullptr, ENGINE_RENDERER_DEMO_ASSET_DIR, /*validation=*/true)) {
        rig.TearDown();
        SKIP("no suitable Vulkan device");
    }
    auto shoot = [&](gpu_test::Image* image) {
        REQUIRE(rig.Render() == FrameStatus::Ok);
        REQUIRE(rig.frame.LastGBufferOk());
        image->width = kResetShotW;
        image->height = kResetShotH;
        std::vector<unsigned char> bytes;
        REQUIRE(rig.Read(&bytes));
        image->rgba.assign(bytes.begin(), bytes.end());
    };
    gpu_test::Image before;
    gpu_test::Image after;
    shoot(&before);

    // A reset with the same adapter and driver: zero tolerance, the two pictures are the same bytes.
    const ApplyResult result = rig.ResetDevice(false); // sync validation was on: this is a real change, so a device reset
    REQUIRE(result.ok);
    CHECK(result.performed == CostClass::DeviceReset);
    shoot(&after);
    CHECK(before.rgba == after.rgba);

    // And both match the blessed golden within its existing tolerance.
    gpu_test::Image golden;
    REQUIRE(gpu_test::ReadPpm(gpu_test::GoldenPath("renderer_demo.ppm").c_str(), &golden));
    gpu_test::CompareOptions co;
    co.tolerance = 6;
    co.edge_tolerance = 90;
    co.max_edge_miss = 0.3;
    co.max_interior_miss = 0.002;
    const gpu_test::CompareResult against_before = gpu_test::Compare(golden, before, co, "reset_before");
    const gpu_test::CompareResult against_after = gpu_test::Compare(golden, after, co, "reset_after");
    INFO(against_before.message);
    CHECK(against_before.same);
    INFO(against_after.message);
    CHECK(against_after.same);

    rig.TearDown();
    fx.ExpectErrors(0);
}

TEST_CASE("Resets in a loop on a real device leave the counts, memory and descriptors where they were", "[rhi][vulkan][gpu][reset]") {
    gpu_test::GpuFixture fx;
    fx.Require();
    reset_rig::Rig rig;
    if (!rig.Create(rhi::Backend::Vulkan, 64, 8, false, nullptr, nullptr, /*validation=*/true)) {
        rig.TearDown();
        SKIP("no suitable Vulkan device");
    }
    REQUIRE(rig.Render() == FrameStatus::Ok);
    rig.host.GetDevice()->Quiesce();
    rhi::ResourceCounts first = rig.host.GetDevice()->Resources();
    const usize owners = rig.registry.Count();
    const int iterations = Sanitized() ? 20 : 100;
    // Warm the loop up before the baseline: driver caches and allocator pools settle in the first iterations. Memory is compared by its
    // lowest reading over resets 5 to 9 and over the last five: a leak per reset raises the low point. On Windows (AMD Radeon 860M,
    // Day 161) the process holds about 130 MiB more after some resets and gives it back a few resets later, while the device's own
    // memory (checked by ExpectSameCounts) does not change, so one reading can land on a step.
    i64 early_low = 0;
    i64 late_low = 0;
    i64 baseline_handles = 0;
    for (int i = 0; i < iterations; ++i) {
        INFO("reset " << i);
        REQUIRE(rig.ResetDevice(i % 2 != 0).ok); // alternates; the first call turns sync validation off
        REQUIRE(rig.Render() == FrameStatus::Ok);
        rig.host.GetDevice()->Quiesce();
        const rhi::ResourceCounts now = rig.host.GetDevice()->Resources();
        if (i == 0) {
            first = now; // the first frame after a reset caches its frame and object bind groups; every later reset must match this
        }
        reset_rig::ExpectSameCounts(now, first);
        CHECK(rig.registry.Count() == owners);
        if (i == 4) baseline_handles = os::OpenHandleCount();
        if ((i >= 4 && i < 9) || i >= iterations - 5) {
            const i64 resident = os::ResidentMemoryKiB();
            i64& low = i < 9 ? early_low : late_low;
            low = (low == 0 || resident < low) ? resident : low;
        }
    }
    const i64 drift_kib = late_low - early_low;
    const i64 handle_drift = os::OpenHandleCount() - baseline_handles;
    WARN("after " << iterations << " resets: lowest resident " << early_low << " KiB over resets 5-9, " << late_low << " KiB over the last five (drift "
                  << drift_kib << " KiB), handle drift " << handle_drift);
    CHECK(handle_drift <= 2);
    if (!Sanitized() && early_low != 0 && late_low != 0) {
        i64 allowed_kib = 8 * 1024;
#if ENGINE_PLATFORM_WINDOWS
        allowed_kib += 4 * 64 * 1024; // the old devices' memory Windows still counts in the process: up to four 64 MiB blocks (Day 161)
#endif
        CHECK(drift_kib < allowed_kib);
    }
    CHECK(rig.R().Stats().resets == static_cast<u64>(iterations));
    CHECK(rig.R().Stats().rollbacks == 0);
    CHECK(rig.host.LeakedObjects() == 0);
    rig.TearDown();
    CHECK(rig.host.LeakedObjects() == 0);
    fx.ExpectErrors(0);
}
