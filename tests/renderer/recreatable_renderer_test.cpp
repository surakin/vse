// Phase 7 Day 109: materials, passes, frame buffers and the transient pool across a device reset.
#include <algorithm>
#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "demo_frame.h"
#include "demo_scene.h"
#include "renderer/device_resources.h"
#include "renderer/texture.h"
#include "rhi/device_host.h"
#include "rhi/null/null_device.h"
#include "rhi/rhi_log.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;

namespace {

struct RecQuietLog {
    bool saved = rhi::LogRhi.Enabled();
    RecQuietLog() { rhi::LogRhi.SetEnabled(false); }
    ~RecQuietLog() { rhi::LogRhi.SetEnabled(saved); }
};

constexpr u32 kWidth = 96;
constexpr u32 kHeight = 64;

// The deferred frame on a device the host can replace: every part registers with one registry.
struct ResetRig {
    DeviceResources registry; // declared first: destroyed last, after the host has told its observers
    rhi::DeviceHost host;
    rhi::TextureHandle target; // an offscreen render target; the rig makes it again after a reset (it has no pixels to keep)
    renderer_demo::DemoFrame frame;
    renderer_demo::DemoScene scene;
    Camera camera;
    rhi::DeviceCreateInfo info;

    bool Create(bool prewarm, rhi::Backend backend = rhi::Backend::Null) {
        info.backend = backend;
        host.AddObserver(&registry, 0);
        if (!host.Create(info, rhi::SwapchainDesc{})) return false;
        if (!MakeTarget()) return false;
        renderer_demo::FrameSetup fs;
        fs.device = host.GetDevice();
        fs.max_objects = 128;
        fs.max_lights = 32;
        fs.resources = &registry;
        fs.prewarm_pipelines = prewarm;
        if (!frame.Create(fs)) return false;
        renderer_demo::SceneOptions so;
        so.objects = 64;
        so.lights = 8;
        so.resources = &registry;
        if (!scene.Create(host.GetDevice(), frame.Materials(), frame.Templates(), so)) return false;
        scene.Animate(0.0f);
        camera = scene.OverviewCamera(static_cast<f32>(kWidth) / kHeight);
        FitCameraDepthRange(scene.Scene(), &camera, 0.1f);
        return true;
    }
    bool MakeTarget() {
        rhi::TextureDesc td;
        td.width = kWidth;
        td.height = kHeight;
        td.format = rhi::Format::RGBA8_SRGB;
        td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
        target = host.GetDevice()->CreateTexture(td);
        return target.IsValid();
    }
    FrameStatus Render() {
        FrameTarget ft;
        ft.texture = target;
        ft.width = kWidth;
        ft.height = kHeight;
        ft.format = rhi::Format::RGBA8_SRGB;
        ft.final_state = rhi::ResourceState::CopySrc;
        return frame.Render(&scene.Scene(), &camera, &ft);
    }
    bool Reset() {
        host.GetDevice()->WaitIdle();
        host.GetDevice()->DestroyTexture(target); // the host destroys the device next; the target is the rig's own
        target = {};
        const bool ok = host.Recreate(info, rhi::SwapchainDesc{});
        if (ok) {
            frame.Rebind(host.GetDevice(), nullptr);
            scene.Rebind(host.GetDevice());
            return MakeTarget();
        }
        return false;
    }
    void TearDown() {
        host.GetDevice()->WaitIdle();
        scene.Destroy();
        frame.Destroy();
        if (target.IsValid()) host.GetDevice()->DestroyTexture(target);
        target = {};
        host.Destroy();
    }
    rhi::NullDevice* Null() { return static_cast<rhi::NullDevice*>(host.GetDevice()); }
};

// The first bytes of a material's parameter block as the GPU will read them in the current frame slot.
std::vector<unsigned char> ParameterBytes(rhi::NullDevice* device, MaterialSystem* materials, MaterialId id, u64 frame) {
    materials->BeginFrame(frame);
    materials->FlushMaterials();
    const BufferRegion region = materials->ParameterRegion(id);
    std::vector<unsigned char> bytes(static_cast<usize>(region.size));
    REQUIRE(device->PeekBuffer(region.buffer, region.offset, bytes.data(), bytes.size()));
    return bytes;
}

} // namespace

TEST_CASE("The deferred frame renders the same after a reset and every part is rebuilt", "[recreatable][renderer]") {
    RecQuietLog quiet;
    ResetRig rig;
    REQUIRE(rig.Create(false));
    REQUIRE(rig.Render() == FrameStatus::Ok);
    const GBufferStats before = rig.frame.GBufferStatistics();
    const LightSummary lights_before = rig.frame.Lights();
    CHECK(before.drawn_objects > 0);

    REQUIRE(rig.Reset());
    CHECK(rig.registry.FailedOwner() == nullptr);
    REQUIRE(rig.Render() == FrameStatus::Ok);
    CHECK(rig.frame.LastGBufferOk());
    const GBufferStats after = rig.frame.GBufferStatistics();
    CHECK(after.drawn_objects == before.drawn_objects);
    CHECK(after.draws == before.draws);
    CHECK(rig.frame.Lights().point_count == lights_before.point_count);
    CHECK(rig.Null()->ErrorCount() == 0);
    rig.TearDown();
}

TEST_CASE("Material parameters and bindings are identical after a reset; a bound texture follows the owner's new handle", "[recreatable][material]") {
    RecQuietLog quiet;
    ResetRig rig;
    REQUIRE(rig.Create(false));
    MaterialSystem* materials = rig.frame.Materials();
    MaterialId bumps;
    for (u32 i = 0; i < 16; ++i) {
        const MaterialId id = materials->MaterialAtIndex(i);
        if (id.IsValid() && materials->EffectiveTexture(id, "normal_tex").IsValid() &&
            !(materials->EffectiveTexture(id, "normal_tex") == materials->GetDefaultTexture(DefaultTexture::FlatNormal))) {
            bumps = id;
        }
    }
    REQUIRE(bumps.IsValid());
    std::vector<u32> unbound; // materials whose albedo slot holds no texture of their own
    for (u32 i = 0; i < 16; ++i) {
        const MaterialId id = materials->MaterialAtIndex(i);
        if (id.IsValid() && materials->EffectiveTexture(id, "albedo_tex") == materials->GetDefaultTexture(DefaultTexture::White)) unbound.push_back(i);
    }
    const std::vector<unsigned char> params_before = ParameterBytes(rig.Null(), materials, bumps, 1);
    const rhi::TextureHandle bound_before = materials->EffectiveTexture(bumps, "normal_tex");

    REQUIRE(rig.Reset());
    const std::vector<unsigned char> params_after = ParameterBytes(rig.Null(), materials, bumps, 1);
    CHECK(params_after == params_before);
    const rhi::TextureHandle bound_after = materials->EffectiveTexture(bumps, "normal_tex");
    CHECK(bound_after != bound_before);       // it follows the texture owner's new handle ...
    CHECK(bound_after.IsValid());
    CHECK(bound_after != materials->GetDefaultTexture(DefaultTexture::FlatNormal)); // ... and is not the default slot
    // The default-slot rule: a material with no albedo texture still reads the default texture of the new device.
    u32 checked = 0;
    for (u32 i = 0; i < 16; ++i) {
        const MaterialId id = materials->MaterialAtIndex(i);
        if (id.IsValid() && std::find(unbound.begin(), unbound.end(), i) != unbound.end()) {
            CHECK(materials->EffectiveTexture(id, "albedo_tex") == materials->GetDefaultTexture(DefaultTexture::White));
            ++checked;
        }
    }
    CHECK(checked == unbound.size());
    CHECK(!unbound.empty());
    CHECK(materials->Stats().rebuilds == 1);
    CHECK(rig.Render() == FrameStatus::Ok);
    rig.TearDown();
}

TEST_CASE("Pipelines are rebuilt lazily by default and eagerly with prewarm", "[recreatable][material]") {
    RecQuietLog quiet;
    for (const bool prewarm : {false, true}) {
        INFO("prewarm " << prewarm);
        ResetRig rig;
        REQUIRE(rig.Create(prewarm));
        REQUIRE(rig.Render() == FrameStatus::Ok);
        const u32 pipelines = rig.frame.Materials()->Stats().pipelines;
        CHECK(pipelines > 0);
        REQUIRE(rig.Reset());
        if (prewarm) {
            CHECK(rig.frame.Materials()->Stats().pipelines == pipelines); // rebuilt in the reset
        } else {
            CHECK(rig.frame.Materials()->Stats().pipelines == 0); // nothing until first use
        }
        REQUIRE(rig.Render() == FrameStatus::Ok);
        CHECK(rig.frame.Materials()->Stats().pipelines == pipelines);
        rig.TearDown();
    }
}

TEST_CASE("The transient pool and the frame buffers are rebuilt and the device holds nothing extra", "[recreatable][pool]") {
    RecQuietLog quiet;
    ResetRig rig;
    REQUIRE(rig.Create(false));
    REQUIRE(rig.Render() == FrameStatus::Ok);
    CHECK(rig.frame.PoolStats().textures_live > 0);
    rhi::ResourceCounts steady;
    {
        rig.host.GetDevice()->Quiesce();
        steady = rig.host.GetDevice()->Resources();
    }
    REQUIRE(rig.Reset());
    CHECK(rig.frame.PoolStats().textures_live == 0); // released, made again on demand
    CHECK(rig.frame.PoolStats().resets == 1);
    CHECK(rig.Render() == FrameStatus::Ok);
    rig.host.GetDevice()->Quiesce();
    const rhi::ResourceCounts again = rig.host.GetDevice()->Resources();
    CHECK(again.buffers == steady.buffers);
    CHECK(again.textures == steady.textures);
    CHECK(again.samplers == steady.samplers);
    CHECK(again.shaders == steady.shaders);
    CHECK(again.pipelines == steady.pipelines);
    CHECK(again.bind_group_layouts == steady.bind_group_layouts);
    rig.TearDown();
}

TEST_CASE("Several resets in a row leave the counts unchanged", "[recreatable][renderer]") {
    RecQuietLog quiet;
    ResetRig rig;
    REQUIRE(rig.Create(true));
    REQUIRE(rig.Render() == FrameStatus::Ok);
    rig.host.GetDevice()->Quiesce();
    const rhi::ResourceCounts first = rig.host.GetDevice()->Resources();
    for (int i = 0; i < 5; ++i) {
        INFO("reset " << i);
        REQUIRE(rig.Reset());
        REQUIRE(rig.Render() == FrameStatus::Ok);
        rig.host.GetDevice()->Quiesce();
        const rhi::ResourceCounts now = rig.host.GetDevice()->Resources();
        CHECK(now.UserObjects() == first.UserObjects());
        CHECK(now.pipelines == first.pipelines);
    }
    rig.TearDown();
}

TEST_CASE("An offscreen frame is pixel-identical before and after a reset on a real device", "[recreatable][renderer][vulkan][gpu]") {
    RecQuietLog quiet;
    ResetRig rig;
    if (!rig.Create(false, rhi::Backend::Vulkan)) {
        rig.host.Destroy();
        SKIP("no suitable Vulkan device");
    }
    auto render_and_read = [&](std::vector<unsigned char>* out) {
        REQUIRE(rig.Render() == FrameStatus::Ok);
        rig.host.GetDevice()->WaitIdle();
        out->assign(static_cast<usize>(kWidth) * kHeight * 4, 0);
        REQUIRE(rig.host.GetDevice()->ReadbackTexture(rig.target, 0, 0, out->data(), out->size(), rhi::ResourceState::CopySrc));
    };
    std::vector<unsigned char> before;
    std::vector<unsigned char> after;
    render_and_read(&before);
    REQUIRE(rig.Reset());
    render_and_read(&after);
    CHECK(before == after);
    bool any_lit = false;
    for (usize i = 0; i + 3 < before.size() && !any_lit; i += 4) any_lit = before[i] != 0 || before[i + 1] != 0 || before[i + 2] != 0;
    CHECK(any_lit);
    rig.TearDown();
}
