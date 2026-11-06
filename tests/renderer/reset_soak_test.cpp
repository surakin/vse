// Phase 8 Day 112: the acceptance tests of the phase on the null backend: the reset loop, a reset during a frame sequence, what
// each cost class does not do, and a reset while the job scheduler is running.
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "jobs/scheduler.h"
#include "renderer/texture.h"
#include "reset_rig.h"
#include "rhi/null/null_device.h"
#include "rhi/rhi_log.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;
using namespace engine_placeholder::renderer::settings;

namespace {

struct SoakQuietLog {
    bool saved = rhi::LogRhi.Enabled();
    SoakQuietLog() { rhi::LogRhi.SetEnabled(false); }
    ~SoakQuietLog() { rhi::LogRhi.SetEnabled(saved); }
};

using reset_rig::ExpectSameCounts;

} // namespace

TEST_CASE("100 device resets leave the counts, the registry and the statistics where they were", "[reset_soak]") {
    SoakQuietLog quiet;
    reset_rig::Rig rig;
    REQUIRE(rig.Create(rhi::Backend::Null));
    REQUIRE(rig.Render() == FrameStatus::Ok);
    rig.host.GetDevice()->Quiesce();
    const rhi::ResourceCounts first = rig.host.GetDevice()->Resources();
    const usize owners = rig.registry.Count();
    CHECK(first.UserObjects() > 0);
    for (int i = 0; i < 100; ++i) {
        INFO("reset " << i);
        const ApplyResult result = rig.ResetDevice(i % 2 == 0);
        REQUIRE(result.ok);
        REQUIRE(result.performed == CostClass::DeviceReset);
        REQUIRE(rig.Render() == FrameStatus::Ok);
        rig.host.GetDevice()->Quiesce();
        ExpectSameCounts(rig.host.GetDevice()->Resources(), first);
        CHECK(rig.registry.Count() == owners);
        CHECK(static_cast<rhi::NullDevice*>(rig.host.GetDevice())->ErrorCount() == 0);
    }
    const RendererStats stats = rig.R().Stats();
    CHECK(stats.resets == 100);
    CHECK(stats.rollbacks == 0);
    CHECK(stats.frames_failed == 0);
    CHECK(stats.frames_lost == 0);
    CHECK(stats.frames_rendered == 101);
    CHECK(rig.registry.FailedOwner() == nullptr);
    CHECK(rig.host.LeakedObjects() == 0); // every owner released its objects before each device was destroyed
    rig.TearDown();
    CHECK(rig.host.LeakedObjects() == 0);
}

TEST_CASE("Resets during a 50-frame sequence keep the scene and the frame numbering", "[reset_soak]") {
    SoakQuietLog quiet;
    reset_rig::Rig rig;
    REQUIRE(rig.Create(rhi::Backend::Null));
    const u32 objects = rig.scene.ObjectCount();
    const u32 lights = rig.scene.LightCount();
    const f32 aspect = static_cast<f32>(rig.target.width) / static_cast<f32>(rig.target.height);
    // The first material's parameter block as the GPU reads it, before any reset.
    MaterialSystem* materials = rig.frame.Materials();
    const MaterialId probe = materials->MaterialAtIndex(0);
    REQUIRE(probe.IsValid());
    auto parameters = [&]() {
        materials->BeginFrame(1);
        materials->FlushMaterials();
        const BufferRegion region = materials->ParameterRegion(probe);
        std::vector<unsigned char> bytes(static_cast<usize>(region.size));
        REQUIRE(static_cast<rhi::NullDevice*>(rig.host.GetDevice())->PeekBuffer(region.buffer, region.offset, bytes.data(), bytes.size()));
        return bytes;
    };
    const std::vector<unsigned char> parameters_before = parameters();
    u32 resets = 0;
    for (u32 frame = 1; frame <= 50; ++frame) {
        if (frame == 7 || frame == 8 || frame == 31) {
            REQUIRE(rig.ResetDevice(frame % 2 == 1).ok);
            ++resets;
        } else if (frame == 20) {
            RenderSettings wanted = rig.R().Settings();
            wanted.SetEnum(kPresentMode, 1); // a swapchain-class change
            const ApplyResult result = rig.R().ApplySettings(wanted);
            REQUIRE(result.ok);
            CHECK(result.performed == CostClass::Swapchain);
        }
        // A changing camera.
        rig.scene.Animate(0.1f * static_cast<f32>(frame));
        rig.camera = rig.scene.OverviewCamera(aspect);
        FitCameraDepthRange(rig.scene.Scene(), &rig.camera, 0.1f);
        REQUIRE(rig.Render() == FrameStatus::Ok);
        CHECK(rig.R().FrameIndex() == frame); // the numbering continues across resets
        CHECK(rig.scene.ObjectCount() == objects);
        CHECK(rig.scene.LightCount() == lights);
        CHECK(rig.frame.LastGBufferOk());
    }
    CHECK(resets == 3);
    CHECK(rig.R().Stats().resets == 3);
    CHECK(parameters() == parameters_before); // material parameters are identical by value after three resets
    CHECK(rig.R().Stats().frames_rendered == 50);
    rig.TearDown();
}

TEST_CASE("Each cost class under load leaves untouched what it should", "[reset_soak]") {
    SoakQuietLog quiet;
    reset_rig::Rig rig;
    REQUIRE(rig.Create(rhi::Backend::Null));
    REQUIRE(rig.Render() == FrameStatus::Ok);
    rig.host.GetDevice()->Quiesce();
    const u32 epoch = rig.host.Epoch();
    const rhi::ResourceCounts before = rig.host.GetDevice()->Resources();

    SECTION("a Dynamic change creates nothing and destroys nothing") {
        RenderSettings wanted = rig.R().Settings();
        wanted.SetEnum(kGBufferDebugLayer, 2);
        const ApplyResult result = rig.R().ApplySettings(wanted);
        REQUIRE(result.ok);
        CHECK(result.performed == CostClass::Dynamic);
        CHECK(rig.host.Epoch() == epoch);
        rig.host.GetDevice()->Quiesce();
        ExpectSameCounts(rig.host.GetDevice()->Resources(), before);
        CHECK(rig.R().Stats().resets == 0);
    }
    SECTION("a Swapchain change keeps the device") {
        RenderSettings wanted = rig.R().Settings();
        wanted.SetEnum(kPresentMode, 2);
        const ApplyResult result = rig.R().ApplySettings(wanted);
        REQUIRE(result.ok);
        CHECK(result.performed == CostClass::Swapchain);
        CHECK(rig.host.Epoch() == epoch);
        rig.host.GetDevice()->Quiesce();
        ExpectSameCounts(rig.host.GetDevice()->Resources(), before);
    }
    SECTION("a PipelineRebuild keeps the device and the textures and meshes") {
        const rhi::TextureHandle texture_before = rig.target.handle;
        RenderSettings wanted = rig.R().Settings();
        wanted.SetEnum(kShaderQuality, 0);
        const ApplyResult result = rig.R().ApplySettings(wanted);
        REQUIRE(result.ok);
        CHECK(result.performed == CostClass::PipelineRebuild);
        CHECK(rig.host.Epoch() == epoch);
        CHECK(rig.target.handle == texture_before); // tier 0 was not touched
        REQUIRE(rig.Render() == FrameStatus::Ok);
        rig.host.GetDevice()->Quiesce();
        ExpectSameCounts(rig.host.GetDevice()->Resources(), before);
    }
    rig.TearDown();
}

TEST_CASE("A reset while the job scheduler runs records on the new device afterwards", "[reset_soak][concurrency]") {
    SoakQuietLog quiet;
    jobs::Scheduler::Config config;
    config.participants = 4;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    reset_rig::Rig rig;
    REQUIRE(rig.Create(rhi::Backend::Null, 64, 8, false, &scheduler));
    for (int i = 0; i < 5; ++i) {
        REQUIRE(rig.Render() == FrameStatus::Ok); // passes record on several threads
    }
    for (int i = 0; i < 10; ++i) {
        REQUIRE(rig.ResetDevice(i % 2 == 0).ok);
        REQUIRE(rig.Render() == FrameStatus::Ok);
        REQUIRE(rig.Render() == FrameStatus::Ok);
    }
    CHECK(rig.R().Stats().resets == 10);
    CHECK(static_cast<rhi::NullDevice*>(rig.host.GetDevice())->ErrorCount() == 0);
    rig.TearDown();
}

TEST_CASE("A material follows a texture owner that was rebuilt on its own", "[reset_soak][material]") {
    SoakQuietLog quiet;
    reset_rig::Rig rig;
    REQUIRE(rig.Create(rhi::Backend::Null));
    MaterialSystem* materials = rig.frame.Materials();
    MaterialId plastic;
    for (u32 i = 0; i < 16 && !plastic.IsValid(); ++i) {
        const MaterialId id = materials->MaterialAtIndex(i);
        if (id.IsValid() && materials->EffectiveTexture(id, "albedo_tex") == materials->GetDefaultTexture(DefaultTexture::White)) plastic = id;
    }
    REQUIRE(plastic.IsValid());
    GpuTexture mine;
    TextureImage image;
    image.desc.width = image.desc.height = 4;
    image.desc.format = rhi::Format::RGBA8_UNorm;
    image.desc.usage = rhi::TextureUsage::Sampled;
    image.pixels.Resize(64);
    REQUIRE(mine.Create(rig.host.GetDevice(), static_cast<TextureImage&&>(image), &rig.registry, "mine"));
    REQUIRE(materials->SetTexture(plastic, "albedo_tex", &mine));
    materials->BeginFrame(1);
    materials->FlushMaterials();
    const rhi::TextureHandle first = materials->EffectiveTexture(plastic, "albedo_tex");
    const u32 rebuilds = materials->BindGroupRebuilds(plastic);
    // The owner alone is rebuilt (no device change): the material must pick up the new handle at the next flush.
    mine.Release();
    REQUIRE(mine.Recreate(rig.host.GetDevice()));
    materials->BeginFrame(2);
    materials->FlushMaterials();
    CHECK(materials->EffectiveTexture(plastic, "albedo_tex") == mine.Handle());
    CHECK(mine.Handle() != first);
    CHECK(materials->BindGroupRebuilds(plastic) == rebuilds + 1);
    materials->SetTexture(plastic, "albedo_tex", static_cast<const GpuTexture*>(nullptr)); // unbind before `mine` goes
    mine.Destroy();
    rig.TearDown();
}
