// Phase 13 Day 172: switching the GPU of a running renderer through its `adapter` setting. Every registered owner rebuilds on the new
// device, the picture matches the blessed golden on each adapter, and the counts and owners stay where they were. (The backend is chosen
// at start: Day 173 removed the run-time Vulkan/DX12 switch this file also tested.)

#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "backend_fixture.h"
#include "build_defs.h"
#include "image_compare.h"
#include "reset_rig.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace {

constexpr u32 kSwitchShotW = 320; // the size of renderer_demo.ppm
constexpr u32 kSwitchShotH = 180;

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

// The live objects of every kind, without device memory (which depends on the adapter's alignment rules).
void ExpectSameObjectCounts(const rhi::ResourceCounts& now, const rhi::ResourceCounts& first) {
    CHECK(now.buffers == first.buffers);
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
