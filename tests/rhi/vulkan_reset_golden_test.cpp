// Phase 8 Day 112: the reset on a real device: the picture before and after a reset is identical and still matches the blessed
// golden image, and a loop of resets leaves nothing behind.

#if !defined(_WIN32)
#include <unistd.h> // sysconf
#endif
#include <cstdio>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "io/directory.h"
#include "io/file.h"
#include "gpu_fixture.h"
#include "image_compare.h"
#include "reset_rig.h"
#include "rhi/rhi_log.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace {

constexpr u32 kResetShotW = 320;
constexpr u32 kResetShotH = 180;

long OpenDescriptorCount() {
    long count = 0;
    io::Directory fds;
    if (io::Directory::Open(io::Path("/proc/self/fd", io::PathStyle::Posix), &fds).Ok()) {
        io::DirectoryEntry entry;
        bool found = false;
        while (fds.Next(&entry, &found).Ok() && found) ++count;
    }
    return count;
}

// The resident set from /proc on Linux. On Windows there is no /proc: 0 (and so no drift) until the reset tests get a Windows measure
// on Day 161 (KNOWN-ISSUES.md). The descriptor count above is 0 there for the same reason.
long ResetResidentKiB() {
#if defined(_WIN32)
    return 0;
#else
    io::Path statm_path("/proc/self/statm", io::PathStyle::Posix);
    Vector<u8> bytes;
    if (io::ReadFile(statm_path, &bytes, 4096).Failed() || bytes.Size() == 0) return 0;
    char text[128] = {};
    std::memcpy(text, bytes.Data(), bytes.Size() < sizeof(text) - 1 ? bytes.Size() : sizeof(text) - 1);
    long pages = 0;
    long resident = 0;
    const int n = std::sscanf(text, "%ld %ld", &pages, &resident);
    return n == 2 ? resident * (sysconf(_SC_PAGESIZE) / 1024) : 0;
#endif
}

// One return per configuration: code after an unconditional return is a warning (C4702) under /WX.
bool Sanitized() {
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    return true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
    return true;
#else
    return false;
#endif
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
    // Warm the loop up before the baseline: driver caches and allocator pools settle in the first iterations.
    long baseline_resident = 0;
    long baseline_fds = 0;
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
        if (i == 4) {
            baseline_resident = ResetResidentKiB();
            baseline_fds = OpenDescriptorCount();
        }
    }
    const long drift_kib = ResetResidentKiB() - baseline_resident;
    const long fd_drift = OpenDescriptorCount() - baseline_fds;
    WARN("after " << iterations << " resets: resident drift " << drift_kib << " KiB since reset 5, descriptor drift " << fd_drift);
    CHECK(fd_drift <= 2);
    if (!Sanitized() && baseline_resident != 0) {
        CHECK(drift_kib < 8 * 1024);
    }
    CHECK(rig.R().Stats().resets == static_cast<u64>(iterations));
    CHECK(rig.R().Stats().rollbacks == 0);
    CHECK(rig.host.LeakedObjects() == 0);
    rig.TearDown();
    CHECK(rig.host.LeakedObjects() == 0);
    fx.ExpectErrors(0);
}
