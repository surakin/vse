// Phase 8 Day 110: Renderer::ApplySettings on the null backend: the cheapest sufficient action, the frame boundary, rollback.
#include "cstr/cstr.h"
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "backend_fixture.h"
#include "renderer/device_resources.h"
#include "renderer/renderer.h"
#include "rhi/backends/backends.h"
#include "rhi/device_host.h"
#include "rhi/null/null_device.h"
#include "rhi/rhi_log.h"
#include "string_view.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;
using namespace engine_placeholder::renderer::settings;

namespace {

struct ApplyQuietLog {
    bool saved = rhi::LogRhi.Enabled();
    ApplyQuietLog() { rhi::LogRhi.SetEnabled(false); }
    ~ApplyQuietLog() { rhi::LogRhi.SetEnabled(saved); }
};

struct ApplyOwner final : Recreatable {
    const char* name = "owner";
    int releases = 0;
    int recreates = 0;
    int fail_next = 0; // fail this many Recreate calls
    bool live = true;
    void Release() override {
        ++releases;
        live = false;
    }
    bool Recreate(rhi::Device*) override {
        if (fail_next > 0) {
            --fail_next;
            return false;
        }
        ++recreates;
        live = true;
        return true;
    }
    bool Live() const override { return live; }
    const char* RecreatableName() const override { return name; }
};

void ClearTarget(const PassExecuteContext& context, void* user) {
    const GraphResource* target = static_cast<const GraphResource*>(user);
    rhi::RenderingDesc rd;
    rd.color[0].texture = context.graph->ResolvedTexture(*target);
    rd.color_count = 1;
    context.list->BeginRendering(rd);
    context.list->EndRendering();
}

struct ApplyRig;
bool BuildFrame(const FrameContext& context, void* user);

struct ApplyRig {
    DeviceResources registry; // destroyed last
    ApplyOwner texture_owner; // tier 0
    ApplyOwner material_owner; // tier 3
    ApplyOwner pass_owner;     // tier 4
    rhi::DeviceHost host;
    rhi::DeviceCreateInfo info;
    RenderSettings initial;
    GraphResource target;
    UniquePtr<Renderer> renderer;
    RenderSettings during_frame; // what the build callback asks for, when set
    bool ask_during_frame = false;
    u32 recovery_attempts = 0;
    ApplyResult asked_result;
    int begins = 0;
    int ends = 0;
    bool last_end_ok = true;

    bool Create(bool with_host = true) {
        texture_owner.name = "texture";
        material_owner.name = "material";
        pass_owner.name = "pass";
        info.backend = rhi::Backend::Null;
        initial.SetEnum(kBackend, 0);
        initial.SetInt(kWidth, 64);
        initial.SetInt(kHeight, 48);
        host.AddObserver(&registry, 0);
        rhi::SwapchainDesc sc;
        sc.width = 64;
        sc.height = 48;
        if (!host.Create(info, sc)) return false;
        registry.Register(&texture_owner, tiers::kTextures);
        registry.Register(&material_owner, tiers::kMaterials);
        registry.Register(&pass_owner, tiers::kPasses);
        RendererDesc rd;
        rd.device = host.GetDevice();
        rd.swapchain = host.GetSwapchain();
        rd.host = with_host ? &host : nullptr;
        rd.create_info = info;
        rd.initial_settings = &initial;
        rd.resources = &registry;
        rd.recovery.max_attempts = recovery_attempts; // 0: a change that cannot be undone is terminal
        rd.build = &BuildFrame;
        rd.build_user = this;
        renderer = MakeUnique<Renderer>(rd);
        renderer->SetResetCallbacks(
            [](void* user, const char*) { ++static_cast<ApplyRig*>(user)->begins; },
            [](void* user, bool ok) {
                auto* self = static_cast<ApplyRig*>(user);
                ++self->ends;
                self->last_end_ok = ok;
            },
            this);
        return true;
    }
    FrameStatus Render() { return renderer->RenderFrame(nullptr, nullptr, nullptr); }
    u32 Epoch() { return host.Epoch(); }
};

bool BuildFrame(const FrameContext& context, void* user) {
    auto* rig = static_cast<ApplyRig*>(user);
    if (rig->ask_during_frame) {
        rig->asked_result = rig->renderer->ApplySettings(rig->during_frame);
    }
    GraphResource* target = &rig->target;
    *target = context.target;
    context.graph->AddPass("clear", &ClearTarget, target).Write(*target, GraphUsage::ColorAttachment);
    return true;
}

} // namespace

TEST_CASE("A Dynamic change stores the value and touches nothing else", "[apply_settings]") {
    ApplyQuietLog quiet;
    ApplyRig rig;
    REQUIRE(rig.Create());
    REQUIRE(rig.Render() == FrameStatus::Ok);
    const u32 epoch = rig.Epoch();
    rhi::Swapchain* swapchain = rig.host.GetSwapchain();
    const u64 generation = rig.renderer->Settings().Generation();

    RenderSettings wanted = rig.renderer->Settings();
    wanted.SetEnum(kGBufferDebugLayer, 2);
    const ApplyResult result = rig.renderer->ApplySettings(wanted);
    CHECK(result.ok);
    CHECK(result.performed == CostClass::Dynamic);
    CHECK(rig.renderer->Settings().GetEnum(kGBufferDebugLayer) == 2);
    CHECK(rig.renderer->Settings().Generation() != generation);
    CHECK(rig.Epoch() == epoch);                       // no device re-created
    CHECK(rig.host.GetSwapchain() == swapchain);       // no swapchain re-created
    CHECK(rig.begins == 0);                            // the GPU was not touched, so no callback
    CHECK(rig.texture_owner.releases == 0);
    CHECK(rig.renderer->Stats().applies[static_cast<usize>(CostClass::Dynamic)] == 1);
    CHECK(rig.Render() == FrameStatus::Ok);
}

TEST_CASE("Applying equal settings does nothing, also after a text round trip", "[apply_settings]") {
    ApplyRig rig;
    REQUIRE(rig.Create());
    char text[4096];
    const usize n = SerializeSettings(rig.renderer->Settings(), text, sizeof(text));
    RenderSettings parsed;
    SettingsParseReport report;
    REQUIRE(ParseSettings(text, n, &parsed, &report));
    const ApplyResult result = rig.renderer->ApplySettings(parsed);
    CHECK(result.ok);
    CHECK_FALSE(result.change.Any());
    CHECK(result.performed == CostClass::None);
    CHECK(rig.begins == 0);
}

TEST_CASE("A Swapchain change reconfigures the swapchain and keeps the device", "[apply_settings]") {
    ApplyQuietLog quiet;
    ApplyRig rig;
    REQUIRE(rig.Create());
    const u32 epoch = rig.Epoch();
    RenderSettings wanted = rig.renderer->Settings();
    wanted.SetEnum(kPresentMode, 2);
    wanted.SetInt(kWidth, 80);
    wanted.SetInt(kHeight, 60);
    const ApplyResult result = rig.renderer->ApplySettings(wanted);
    REQUIRE(result.ok);
    CHECK(result.performed == CostClass::Swapchain);
    CHECK(rig.Epoch() == epoch);
    CHECK(rig.host.GetSwapchain()->Width() == 80);
    CHECK(rig.host.GetSwapchain()->ActualPresentMode() == rhi::PresentMode::Immediate);
    CHECK(rig.begins == 1);
    CHECK(rig.ends == 1);
    CHECK(rig.texture_owner.releases == 0);
    CHECK(rig.Render() == FrameStatus::Ok);
}

TEST_CASE("A PipelineRebuild change rebuilds only the upper tiers", "[apply_settings]") {
    ApplyQuietLog quiet;
    ApplyRig rig;
    REQUIRE(rig.Create());
    const u32 epoch = rig.Epoch();
    RenderSettings wanted = rig.renderer->Settings();
    wanted.SetEnum(kShaderQuality, 0);
    const ApplyResult result = rig.renderer->ApplySettings(wanted);
    REQUIRE(result.ok);
    CHECK(result.performed == CostClass::PipelineRebuild);
    CHECK(rig.Epoch() == epoch);
    CHECK(rig.texture_owner.releases == 0); // tier 0 stays
    CHECK(rig.material_owner.releases == 1);
    CHECK(rig.pass_owner.releases == 1);
    CHECK(rig.material_owner.recreates == 1);
    CHECK(rig.pass_owner.recreates == 1);
    CHECK(rig.Render() == FrameStatus::Ok);
}

TEST_CASE("A DeviceReset change replaces the device and every owner rebuilds", "[apply_settings]") {
    ApplyQuietLog quiet;
    ApplyRig rig;
    REQUIRE(rig.Create());
    const u32 epoch = rig.Epoch();
    RenderSettings wanted = rig.renderer->Settings();
    wanted.SetText(kAdapter, "null-integrated");
    const ApplyResult result = rig.renderer->ApplySettings(wanted);
    REQUIRE(result.ok);
    CHECK(result.performed == CostClass::DeviceReset);
    CHECK(rig.Epoch() != epoch);
    CHECK(CStrCompare(rig.host.GetDevice()->Adapter().id, "null-integrated") == 0);
    CHECK(rig.renderer->GetDevice() == rig.host.GetDevice());
    CHECK(rig.renderer->GetSwapchain() == rig.host.GetSwapchain());
    CHECK(rig.texture_owner.recreates == 1);
    CHECK(rig.material_owner.recreates == 1);
    CHECK(rig.pass_owner.recreates == 1);
    CHECK(rig.renderer->Stats().resets == 1);
    CHECK(result.total_ms >= 0.0);
    CHECK(rig.Render() == FrameStatus::Ok);
    // The adapter setting persists: the next reset keeps it.
    RenderSettings again = rig.renderer->Settings();
    again.SetBool(kSyncValidation, true);
    REQUIRE(rig.renderer->ApplySettings(again).ok);
    CHECK(CStrCompare(rig.host.GetDevice()->Adapter().id, "null-integrated") == 0);
}

TEST_CASE("A change asked for inside a frame waits for the next frame, and a newer request replaces it", "[apply_settings]") {
    ApplyQuietLog quiet;
    ApplyRig rig;
    REQUIRE(rig.Create());
    const u32 epoch = rig.Epoch();
    rig.ask_during_frame = true;
    rig.during_frame = rig.renderer->Settings();
    rig.during_frame.SetText(kAdapter, "null-integrated");
    REQUIRE(rig.Render() == FrameStatus::Ok);
    CHECK(rig.asked_result.queued);
    CHECK(rig.Epoch() == epoch); // nothing happened inside the frame
    rig.ask_during_frame = false;
    REQUIRE(rig.Render() == FrameStatus::Ok); // the queued change is applied at the start of this frame
    CHECK(rig.Epoch() != epoch);
    CHECK(CStrCompare(rig.host.GetDevice()->Adapter().id, "null-integrated") == 0);
    CHECK(rig.renderer->LastApply().performed == CostClass::DeviceReset);

    // Two requests inside two frames: the second replaces the first.
    rig.ask_during_frame = true;
    rig.during_frame = rig.renderer->Settings();
    rig.during_frame.SetText(kAdapter, "null-discrete");
    REQUIRE(rig.Render() == FrameStatus::Ok);
    rig.during_frame.SetText(kAdapter, "null-integrated");
    rig.ask_during_frame = true;
    const u32 before = rig.Epoch();
    REQUIRE(rig.Render() == FrameStatus::Ok); // applies "null-discrete" first at the boundary, then queues the new one
    CHECK(rig.Epoch() != before);
}

TEST_CASE("A failed device creation restores the previous settings", "[apply_settings]") {
    ApplyQuietLog quiet;
    ApplyRig rig;
    REQUIRE(rig.Create());
    RenderSettings wanted = rig.renderer->Settings();
    wanted.SetText(kAdapter, "no-such-adapter");
    const ApplyResult result = rig.renderer->ApplySettings(wanted);
    CHECK_FALSE(result.ok);
    CHECK(result.rolled_back);
    CHECK_FALSE(result.device_lost);
    CHECK(CStrLength(result.reason) > 0);
    CHECK(rig.renderer->Settings().GetText(kAdapter)[0] == '\0'); // unchanged
    CHECK(rig.renderer->Stats().rollbacks == 1);
    CHECK(rig.ends == 1);
    CHECK_FALSE(rig.last_end_ok);
    CHECK(rig.renderer->GetDevice() == rig.host.GetDevice());     // the renderer follows the new (restored) device
    CHECK(rig.Render() == FrameStatus::Ok);
    CHECK(rig.pass_owner.live);
}

TEST_CASE("An owner that fails to rebuild undoes the change", "[apply_settings]") {
    ApplyQuietLog quiet;
    ApplyRig rig;
    REQUIRE(rig.Create());
    rig.material_owner.fail_next = 1; // the first Recreate (in the reset) fails, the one in the rollback works
    RenderSettings wanted = rig.renderer->Settings();
    wanted.SetText(kAdapter, "null-integrated");
    const ApplyResult result = rig.renderer->ApplySettings(wanted);
    CHECK_FALSE(result.ok);
    CHECK(result.rolled_back);
    CHECK(CStrCompare(rig.host.GetDevice()->Adapter().id, "null-discrete") == 0); // the previous adapter is back
    CHECK(rig.renderer->Settings().GetText(kAdapter)[0] == '\0');
    CHECK(rig.Render() == FrameStatus::Ok);
}

TEST_CASE("When the rollback fails too the renderer is device-lost and says so", "[apply_settings]") {
    ApplyQuietLog quiet;
    ApplyRig rig;
    REQUIRE(rig.Create());
    rig.material_owner.fail_next = 2; // the reset and the rollback both fail
    RenderSettings wanted = rig.renderer->Settings();
    wanted.SetText(kAdapter, "null-integrated");
    const ApplyResult result = rig.renderer->ApplySettings(wanted);
    CHECK_FALSE(result.ok);
    CHECK(result.device_lost);
    CHECK(rig.renderer->IsBroken());
    CHECK(rig.Render() == FrameStatus::DeviceLost);
    CHECK(rig.renderer->Stats().frames_lost == 1);
    CHECK_FALSE(rig.renderer->ApplySettings(wanted).ok); // no further changes on a renderer without a device
}

TEST_CASE("A swapchain that cannot be created fails the reset and the rollback", "[apply_settings]") {
    ApplyQuietLog quiet;
    ApplyRig rig;
    rig.info.faults.fail_swapchain_create = false;
    REQUIRE(rig.Create());
    // A renderer whose create_info makes every new swapchain fail: the replacement and the rollback both fail.
    RendererDesc rd;
    rd.device = rig.host.GetDevice();
    rd.swapchain = rig.host.GetSwapchain();
    rd.host = &rig.host;
    rd.create_info = rig.info;
    rd.create_info.faults.fail_swapchain_create = true;
    rd.initial_settings = &rig.initial;
    rd.resources = &rig.registry;
    rd.recovery.max_attempts = 0;
    rd.build = &BuildFrame;
    rd.build_user = &rig;
    Renderer faulty(rd);
    RenderSettings wanted = faulty.Settings();
    wanted.SetText(kAdapter, "null-integrated");
    const ApplyResult result = faulty.ApplySettings(wanted);
    CHECK_FALSE(result.ok);
    CHECK(result.device_lost);
    CHECK(faulty.IsBroken());
}

TEST_CASE("A setting that needs a missing capability is left as it was", "[apply_settings]") {
    ApplyRig rig;
    REQUIRE(rig.Create());
    RenderSettings wanted = rig.renderer->Settings();
    wanted.SetBool(kFullscreen, true); // there is no window, so no fullscreen capability
    const ApplyResult result = rig.renderer->ApplySettings(wanted);
    CHECK(result.ok);
    CHECK(result.change.unsupported_count == 1);
    CHECK_FALSE(rig.renderer->Settings().GetBool(kFullscreen));
}

TEST_CASE("Without a device host only None and Dynamic changes can be applied", "[apply_settings]") {
    ApplyRig rig;
    REQUIRE(rig.Create(/*with_host=*/false));
    RenderSettings dynamic = rig.renderer->Settings();
    dynamic.SetEnum(kGBufferDebugLayer, 1);
    CHECK(rig.renderer->ApplySettings(dynamic).ok);
    RenderSettings swapchain = rig.renderer->Settings();
    swapchain.SetEnum(kPresentMode, 1);
    const ApplyResult result = rig.renderer->ApplySettings(swapchain);
    CHECK_FALSE(result.ok);
    CHECK(CStrFind(result.reason, "DeviceHost") != nullptr);
    CHECK(rig.renderer->Settings().GetEnum(kPresentMode) == 0);
}

TEST_CASE("Settings changes on a real device each leave a renderer that draws", "[apply_settings][gpu]") {
    FOR_EACH_GPU_BACKEND(backend) {
        ApplyQuietLog quiet;
        DeviceResources registry;
        rhi::DeviceHost host;
        host.AddObserver(&registry, 0);
        rhi::DeviceCreateInfo info;
        info.backend = backend;
        info.validation = rhi::ValidationSetting::Off;
        if (!host.Create(info, rhi::SwapchainDesc{})) {
            SKIP("no suitable " << gpu_test::BackendTag(backend) << " device");
        }
        RenderSettings initial;
        initial.SetEnum(kBackend, gpu_test::BackendSettingIndex(backend));
        initial.SetEnum(kValidation, 1);
        struct Frame {
            GraphResource target;
        } frame;
        RendererDesc rd;
        rd.device = host.GetDevice();
        rd.host = &host;
        rd.create_info = info;
        rd.initial_settings = &initial;
        rd.resources = &registry;
        rd.build_user = &frame;
        rd.build = [](const FrameContext& context, void* user) {
            auto* f = static_cast<Frame*>(user);
            f->target = context.target;
            context.graph->AddPass("clear", &ClearTarget, &f->target).Write(f->target, GraphUsage::ColorAttachment);
            return true;
        };
        Renderer renderer(rd);

        auto draw = [&]() {
            rhi::TextureDesc td;
            td.width = 32;
            td.height = 32;
            td.format = rhi::Format::RGBA8_UNorm;
            td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
            const rhi::TextureHandle target = renderer.GetDevice()->CreateTexture(td);
            REQUIRE(target.IsValid());
            FrameTarget ft;
            ft.texture = target;
            ft.width = 32;
            ft.height = 32;
            ft.format = rhi::Format::RGBA8_UNorm;
            ft.final_state = rhi::ResourceState::CopySrc;
            CHECK(renderer.RenderFrame(nullptr, nullptr, &ft) == FrameStatus::Ok);
            renderer.GetDevice()->WaitIdle();
            renderer.GetDevice()->DestroyTexture(target);
        };
        draw();
        const u32 epoch = host.Epoch();

        RenderSettings wanted = renderer.Settings();
        wanted.SetEnum(kValidation, 2); // validation off to on: a device reset
        ApplyResult result = renderer.ApplySettings(wanted);
        REQUIRE(result.ok);
        CHECK(result.performed == CostClass::DeviceReset);
        CHECK(host.Epoch() != epoch);
        draw();

        wanted = renderer.Settings();
        wanted.SetInt(kFramesInFlight, 3);
        wanted.SetEnum(kValidation, 1); // and back off
        result = renderer.ApplySettings(wanted);
        REQUIRE(result.ok);
        draw();

        wanted = renderer.Settings();
        wanted.SetEnum(kShaderQuality, 0); // a pipeline rebuild on the real device
        result = renderer.ApplySettings(wanted);
        REQUIRE(result.ok);
        CHECK(result.performed == CostClass::PipelineRebuild);
        draw();
        CHECK(renderer.Stats().resets == 2);
    }
}

TEST_CASE("A swapchain-class change on a device with no swapchain is only stored", "[apply_settings]") {
    ApplyQuietLog quiet;
    DeviceResources registry;
    rhi::DeviceHost host;
    host.AddObserver(&registry, 0);
    rhi::DeviceCreateInfo info;
    info.backend = rhi::Backend::Null;
    REQUIRE(host.Create(info, rhi::SwapchainDesc{})); // no window, no size: no swapchain
    REQUIRE(host.GetSwapchain() == nullptr);
    RendererDesc rd;
    rd.device = host.GetDevice();
    rd.host = &host;
    rd.create_info = info; // no initial settings: they are derived from this (the null backend)
    rd.resources = &registry;
    rd.build = [](const FrameContext&, void*) { return true; };
    Renderer renderer(rd);
    CHECK(renderer.Settings().GetEnum(kBackend) == 0);
    RenderSettings wanted = renderer.Settings();
    wanted.SetEnum(kPresentMode, 1);
    const ApplyResult result = renderer.ApplySettings(wanted);
    CHECK(result.ok);
    CHECK(result.performed == CostClass::Swapchain);
    CHECK_FALSE(renderer.IsBroken());
    CHECK(renderer.Settings().GetEnum(kPresentMode) == 1);
}

TEST_CASE("A request made outside a frame replaces one still queued", "[apply_settings]") {
    ApplyQuietLog quiet;
    ApplyRig rig;
    REQUIRE(rig.Create());
    const u32 epoch = rig.Epoch();
    rig.ask_during_frame = true;
    rig.during_frame = rig.renderer->Settings();
    rig.during_frame.SetText(kAdapter, "null-integrated");
    REQUIRE(rig.Render() == FrameStatus::Ok); // queued A: the device reset
    rig.ask_during_frame = false;
    RenderSettings b = rig.renderer->Settings();
    b.SetEnum(kGBufferDebugLayer, 4);
    REQUIRE(rig.renderer->ApplySettings(b).ok); // B applies now and replaces A
    REQUIRE(rig.Render() == FrameStatus::Ok);
    CHECK(rig.Epoch() == epoch); // A was not applied afterwards
    CHECK(rig.renderer->Settings().GetEnum(kGBufferDebugLayer) == 4);
    CHECK(rig.renderer->Settings().GetText(kAdapter)[0] == '\0');
}

TEST_CASE("A broken renderer hands out no device", "[apply_settings]") {
    ApplyQuietLog quiet;
    ApplyRig rig;
    REQUIRE(rig.Create());
    rig.material_owner.fail_next = 2;
    RenderSettings wanted = rig.renderer->Settings();
    wanted.SetText(kAdapter, "null-integrated");
    REQUIRE(rig.renderer->ApplySettings(wanted).device_lost);
    CHECK(rig.renderer->GetDevice() == nullptr);
    CHECK(rig.renderer->GetSwapchain() == nullptr);
    CHECK(rig.renderer->Stats().rollbacks == 0); // nothing was undone
}

TEST_CASE("With recovery on, a change that cannot be undone is recovered from", "[apply_settings]") {
    ApplyQuietLog quiet;
    ApplyRig rig;
    rig.recovery_attempts = 3;
    REQUIRE(rig.Create());
    rig.material_owner.fail_next = 2; // the reset and the rollback fail; the first recovery attempt works
    RenderSettings wanted = rig.renderer->Settings();
    wanted.SetText(kAdapter, "null-integrated");
    const ApplyResult result = rig.renderer->ApplySettings(wanted);
    CHECK_FALSE(result.ok);
    CHECK(result.device_lost);
    CHECK_FALSE(rig.renderer->IsBroken());           // recovery rebuilt the previous device
    CHECK(rig.renderer->Stats().recoveries == 1);
    CHECK(rig.begins == rig.ends);                   // the begin and end callbacks stay balanced (change, then recovery)
    CHECK(rig.begins == 2);
    CHECK(CStrCompare(rig.host.GetDevice()->Adapter().id, "null-discrete") == 0);
    CHECK(rig.Render() == FrameStatus::Ok);
}

TEST_CASE("A device reset can be forced with the settings unchanged", "[apply_settings]") {
    ApplyQuietLog quiet;
    ApplyRig rig;
    REQUIRE(rig.Create());
    const u32 epoch = rig.Epoch();
    const u64 generation = rig.renderer->Settings().Generation();
    const ApplyResult result = rig.renderer->ForceDeviceReset();
    REQUIRE(result.ok);
    CHECK(result.performed == CostClass::DeviceReset);
    CHECK(rig.Epoch() != epoch);
    CHECK(rig.texture_owner.recreates == 1);
    CHECK(rig.renderer->Settings().Generation() == generation + 1); // a reset on request is still a settings event
    CHECK(rig.Render() == FrameStatus::Ok);
}

// Phase 13 Day 173: there is no run-time backend switch. The backend is chosen at start; a change waits for the next start, and the rest of the
// same request is applied.
TEST_CASE("A backend change at run time waits for the next start and the rest of the request is applied", "[apply_settings]") {
    ApplyQuietLog quiet;
    ApplyRig rig;
    REQUIRE(rig.Create());
    REQUIRE(rig.Render() == FrameStatus::Ok);
    const u32 epoch = rig.Epoch();
    RenderSettings wanted = rig.renderer->Settings();
    wanted.SetEnum(kBackend, 1); // vulkan
    ApplyResult result = rig.renderer->ApplySettings(wanted);
    CHECK(result.ok);
    CHECK(result.performed == CostClass::None);
    CHECK(result.change.changed_count == 0);
    REQUIRE(result.change.restart_count == 1);
    CHECK(result.change.restart[0] == kBackend);
    CHECK(rig.renderer->Settings().GetEnum(kBackend) == 0);
    CHECK(rig.Epoch() == epoch); // the device stayed
    CHECK(rig.begins == 0);

    // With another change in the same request: that one is applied, the backend still waits.
    wanted.SetFloat(kRenderScale, 0.5f);
    result = rig.renderer->ApplySettings(wanted);
    CHECK(result.ok);
    CHECK(result.performed == CostClass::Dynamic);
    REQUIRE(result.change.changed_count == 1);
    CHECK(result.change.changed[0] == kRenderScale);
    CHECK(result.change.restart_count == 1);
    CHECK(rig.renderer->Settings().GetFloat(kRenderScale) == 0.5f);
    CHECK(rig.renderer->Settings().GetEnum(kBackend) == 0);
    CHECK(rig.Epoch() == epoch);

    // A forced reset recreates on the backend the program started with.
    REQUIRE(rig.renderer->ForceDeviceReset().ok);
    CHECK(rig.Epoch() != epoch);
    CHECK(StringView(rig.host.GetDevice()->BackendName()) == "null");
    CHECK(rig.Render() == FrameStatus::Ok);
}
