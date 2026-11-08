// Phase 13 Day 172: switching the GPU and the backend of a running renderer through its settings. Every registered owner rebuilds
// on the new device (with the other backend's shader blobs for a backend switch), the picture matches the blessed golden after each
// switch, and the counts, owners, handles and process memory stay where they were.

#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "backend_fixture.h"
#include "build_defs.h"
#include "containers/array.h"
#include "image_compare.h"
#include "os/os.h"
#include "reset_rig.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace {

constexpr u32 kSwitchShotW = 320; // the size of renderer_demo.ppm
constexpr u32 kSwitchShotH = 180;

// One return per configuration: code after an unconditional return is a warning (C4702) under /WX.
bool SwitchSanitized() {
#if ENGINE_SANITIZED
    return true;
#else
    return false;
#endif
}

bool HasBackend(rhi::Backend backend) {
    const Vector<rhi::Backend> backends = gpu_test::GpuBackends();
    for (usize i = 0; i < backends.Size(); ++i) {
        if (backends[i] == backend) return true;
    }
    return false;
}

// The demo scene of the reset golden test, 540 objects and 300 lights, on `backend` with validation on.
bool CreateSwitchRig(reset_rig::Rig* rig, rhi::Backend backend) {
    rig->target.width = kSwitchShotW;
    rig->target.height = kSwitchShotH;
    return rig->Create(backend, 540, 300, false, nullptr, ENGINE_RENDERER_DEMO_ASSET_DIR, /*validation=*/true);
}

// The blessed demo picture, read once.
const gpu_test::Image& Golden() {
    static const gpu_test::Image golden = [] {
        gpu_test::Image g;
        gpu_test::ReadPpm(gpu_test::GoldenPath("renderer_demo.ppm").c_str(), &g);
        return g;
    }();
    return golden;
}

// Renders a frame, reads it back and checks it against the golden with the reset golden test's tolerance.
void ShootAndCompare(reset_rig::Rig* rig, gpu_test::Image* image, const char* name) {
    REQUIRE(rig->Render() == FrameStatus::Ok);
    REQUIRE(rig->frame.LastGBufferOk());
    REQUIRE(rig->Read(image));
    const gpu_test::Image& golden = Golden();
    REQUIRE(golden.Valid());
    gpu_test::CompareOptions co;
    co.tolerance = 6;
    co.edge_tolerance = 90;
    co.max_edge_miss = 0.3;
    co.max_interior_miss = 0.002;
    const gpu_test::CompareResult result = gpu_test::Compare(golden, *image, co, name);
    INFO(result.message);
    CHECK(result.same);
}

// The live objects of every kind, without device memory (which depends on the adapter's alignment rules). `extra_buffers` is what
// `now`'s device makes on top of `first`'s: the object index stream of a backend whose instance index starts at zero (DX12).
void ExpectSameObjectCounts(const rhi::ResourceCounts& now, const rhi::ResourceCounts& first, i64 extra_buffers = 0) {
    CHECK(static_cast<i64>(now.buffers) == static_cast<i64>(first.buffers) + extra_buffers);
    CHECK(now.textures == first.textures);
    CHECK(now.samplers == first.samplers);
    CHECK(now.shaders == first.shaders);
    CHECK(now.bind_group_layouts == first.bind_group_layouts);
    CHECK(now.bind_groups == first.bind_groups);
    CHECK(now.pipelines == first.pipelines);
    CHECK(now.swapchains == first.swapchains);
}

} // namespace

TEST_CASE("The GPU of a running renderer switches between the hardware adapter and WARP and back", "[rhi][gpu][golden][reset][dx12]") {
#if ENGINE_PLATFORM_WINDOWS
    if (!HasBackend(rhi::Backend::Dx12)) SKIP("no Direct3D 12 device");
    gpu_test::BackendValidation validation(rhi::Backend::Dx12);
    reset_rig::Rig rig;
    if (!CreateSwitchRig(&rig, rhi::Backend::Dx12)) {
        rig.TearDown();
        SKIP("no suitable dx12 device");
    }
    const rhi::AdapterInfo hardware = rig.host.GetDevice()->Adapter();
    INFO("first adapter " << hardware.name);
    if (hardware.kind == rhi::AdapterKind::Software) {
        rig.TearDown();
        SKIP("the first Direct3D 12 adapter is already a software one");
    }
    gpu_test::Image first;
    ShootAndCompare(&rig, &first, "gpu_switch_hardware");
    rig.host.GetDevice()->Quiesce();
    const rhi::ResourceCounts hardware_counts = rig.host.GetDevice()->Resources();
    const usize owners = rig.registry.Count();

    // To WARP, by a name the user would type.
    RenderSettings wanted = rig.R().Settings();
    REQUIRE(wanted.SetText(settings::kAdapter, "Basic Render") == SetStatus::Ok);
    ApplyResult result = rig.R().ApplySettings(wanted);
    INFO(result.reason);
    REQUIRE(result.ok);
    CHECK(result.performed == CostClass::DeviceReset);
    CHECK(rig.host.GetDevice()->Adapter().kind == rhi::AdapterKind::Software);
    CHECK(std::strstr(rig.host.GetDevice()->Adapter().name, "Basic Render") != nullptr);
    gpu_test::Image warp;
    ShootAndCompare(&rig, &warp, "gpu_switch_warp");
    rig.host.GetDevice()->Quiesce();
    const rhi::ResourceCounts warp_counts = rig.host.GetDevice()->Resources();
    ExpectSameObjectCounts(warp_counts, hardware_counts);
    CHECK(rig.registry.Count() == owners);

    // And back, by the saved id.
    wanted = rig.R().Settings();
    REQUIRE(wanted.SetText(settings::kAdapter, hardware.id) == SetStatus::Ok);
    result = rig.R().ApplySettings(wanted);
    INFO(result.reason);
    REQUIRE(result.ok);
    CHECK(std::strcmp(rig.host.GetDevice()->Adapter().id, hardware.id) == 0);
    gpu_test::Image again;
    ShootAndCompare(&rig, &again, "gpu_switch_hardware_again");
    CHECK(again.rgba == first.rgba); // the same adapter and driver: the same bytes
    rig.host.GetDevice()->Quiesce();
    reset_rig::ExpectSameCounts(rig.host.GetDevice()->Resources(), hardware_counts);
    CHECK(rig.registry.Count() == owners);
    CHECK(rig.R().Stats().resets == 2);
    CHECK(rig.R().Stats().rollbacks == 0);
    rig.TearDown();
    CHECK(rig.host.LeakedObjects() == 0);
#else
    SKIP("Direct3D 12 and WARP are Windows'");
#endif
}

TEST_CASE("A running renderer switches between Vulkan and Direct3D 12 with counts and memory flat", "[rhi][gpu][golden][reset]") {
    if (!HasBackend(rhi::Backend::Vulkan) || !HasBackend(rhi::Backend::Dx12)) SKIP("needs both the Vulkan and the Direct3D 12 backend");
    gpu_test::BackendValidation vulkan_validation(rhi::Backend::Vulkan);
    gpu_test::BackendValidation dx12_validation(rhi::Backend::Dx12);
    reset_rig::Rig rig;
    if (!CreateSwitchRig(&rig, rhi::Backend::Vulkan)) {
        rig.TearDown();
        SKIP("no suitable vulkan device");
    }
    Array<gpu_test::Image, 2> first_image;
    Array<rhi::ResourceCounts, 2> first_counts;
    Array<bool, 2> index_stream = {{false, false}};
    gpu_test::Image start;
    ShootAndCompare(&rig, &start, "switch_start");
    const usize owners = rig.registry.Count();
    const int switches = SwitchSanitized() ? 6 : 20;
    // Memory is compared by its lowest reading over switches 4 to 7 and over the last four, as the reset loop does: the process gives
    // a released device's memory back some switches later, so one reading can land on a step.
    i64 early_low = 0;
    i64 late_low = 0;
    i64 baseline_handles = 0;
    for (int i = 0; i < switches; ++i) {
        const int side = i % 2 == 0 ? 1 : 0; // 1: to Direct3D 12, 0: back to Vulkan
        const rhi::Backend to = side == 1 ? rhi::Backend::Dx12 : rhi::Backend::Vulkan;
        INFO("switch " << i << " to " << gpu_test::BackendTag(to));
        RenderSettings wanted = rig.R().Settings();
        wanted.SetEnum(settings::kBackend, gpu_test::BackendSettingIndex(to));
        const ApplyResult result = rig.R().ApplySettings(wanted);
        INFO(result.reason);
        REQUIRE(result.ok);
        CHECK(result.performed == CostClass::DeviceReset);
        CHECK(rig.host.GetDevice()->Caps().shader_format == (to == rhi::Backend::Dx12 ? rhi::ShaderFormat::Dxil : rhi::ShaderFormat::Spirv));
        gpu_test::Image image;
        ShootAndCompare(&rig, &image, side == 1 ? "switch_dx12" : "switch_vulkan");
        rig.host.GetDevice()->Quiesce();
        const rhi::ResourceCounts now = rig.host.GetDevice()->Resources();
        if (i < 2) {
            first_image[side] = image; // the first frame after a reset caches its frame and object bind groups: later visits match it
            first_counts[side] = now;
            index_stream[side] = rig.host.GetDevice()->Caps().object_index_stream;
        } else {
            CHECK(image.rgba == first_image[side].rgba);
            reset_rig::ExpectSameCounts(now, first_counts[side]);
        }
        if (i == 1) { // the two backends hold the same objects
            const i64 extra = (index_stream[1] ? 1 : 0) - (index_stream[0] ? 1 : 0);
            ExpectSameObjectCounts(first_counts[1], first_counts[0], extra);
        }
        CHECK(rig.registry.Count() == owners);
        if (i == 3) baseline_handles = os::OpenHandleCount(); // on Vulkan, like the last switch
        if ((i >= 4 && i < 8) || i >= switches - 4) {
            const i64 resident = os::ResidentMemoryKiB();
            i64& low = i < 8 ? early_low : late_low;
            low = (low == 0 || resident < low) ? resident : low;
        }
    }
    const i64 drift_kib = late_low - early_low;
    const i64 handle_drift = os::OpenHandleCount() - baseline_handles;
    WARN("after " << switches << " backend switches: lowest resident " << early_low << " KiB over switches 4-7, " << late_low
                  << " KiB over the last four (drift " << drift_kib << " KiB), handle drift " << handle_drift << ", last switch "
                  << rig.R().Stats().last_reset_ms << " ms");
    CHECK(handle_drift <= 2);
    if (!SwitchSanitized() && switches >= 16 && early_low != 0 && late_low != 0) {
        i64 allowed_kib = 8 * 1024;
#if ENGINE_PLATFORM_WINDOWS
        allowed_kib += 4 * 64 * 1024; // the old devices' memory Windows still counts in the process (the reset loop's allowance, Day 161)
#endif
        CHECK(drift_kib < allowed_kib);
    }
    CHECK(rig.R().Stats().resets == static_cast<u64>(switches));
    CHECK(rig.R().Stats().rollbacks == 0);
    rig.TearDown();
    CHECK(rig.host.LeakedObjects() == 0);
}
