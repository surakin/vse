// Phase 8 Day 111: a lost device recovers through the settings path, with a retry limit, a fallback adapter and reporting.
#include <atomic>
#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "io/directory.h"
#include "io/file.h"
#include "renderer/device_resources.h"
#include "renderer/renderer.h"
#include "rhi/adapter.h"
#include "rhi/device_host.h"
#include "rhi/null/null_device.h"
#include "rhi/rhi_log.h"

using namespace engine_placeholder;
using namespace engine_placeholder::renderer;
using namespace engine_placeholder::renderer::settings;

namespace {

struct LossQuietLog {
    bool saved = rhi::LogRhi.Enabled();
    LossQuietLog() { rhi::LogRhi.SetEnabled(false); }
    ~LossQuietLog() { rhi::LogRhi.SetEnabled(saved); }
};

struct NullAdaptersRestore {
    ~NullAdaptersRestore() { rhi::SetNullAdapters(nullptr); }
};

struct LossOwner final : Recreatable {
    int recreates = 0;
    int fail_next = 0;
    bool live = true;
    void Release() override { live = false; }
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
    const char* RecreatableName() const override { return "loss owner"; }
};

void LossClear(const PassExecuteContext& context, void* user) {
    const GraphResource* target = static_cast<const GraphResource*>(user);
    rhi::RenderingDesc rd;
    rd.color[0].texture = context.graph->ResolvedTexture(*target);
    rd.color_count = 1;
    context.list->BeginRendering(rd);
    context.list->EndRendering();
}

struct LossRig;
bool LossBuild(const FrameContext& context, void* user);

struct LossRig {
    DeviceResources registry;
    LossOwner owner;
    rhi::DeviceHost host;
    rhi::DeviceCreateInfo info;
    RenderSettings initial;
    GraphResource target;
    UniquePtr<Renderer> renderer;
    std::atomic<u32> strikes{1};
    bool ask_during_frame = false;
    RenderSettings asked;
    int begins = 0;
    int ends_ok = 0;
    int ends_failed = 0;
    char last_stage[16] = {};

    void Create(const RecoveryPolicy& policy, const char* adapter = nullptr) {
        info.backend = rhi::Backend::Null;
        info.faults.strikes_left = &strikes;
        initial.SetEnum(kBackend, 0);
        initial.SetInt(kWidth, 64);
        initial.SetInt(kHeight, 48);
        if (adapter != nullptr) initial.SetText(kAdapter, adapter);
        rhi::DeviceCreateInfo first = info;
        first.adapter = adapter;
        first.faults = rhi::FaultInjection{}; // the first device does not carry the injected loss
        host.AddObserver(&registry, 0);
        rhi::SwapchainDesc sc;
        sc.width = 64;
        sc.height = 48;
        REQUIRE(host.Create(first, sc));
        registry.Register(&owner, tiers::kPasses);
        RendererDesc rd;
        rd.device = host.GetDevice();
        rd.swapchain = host.GetSwapchain();
        rd.host = &host;
        rd.create_info = info; // every later device carries the faults
        rd.initial_settings = &initial;
        rd.resources = &registry;
        rd.recovery = policy;
        rd.build = &LossBuild;
        rd.build_user = this;
        renderer = MakeUnique<Renderer>(rd);
        renderer->SetResetCallbacks(
            [](void* user, const char* stage) {
                auto* self = static_cast<LossRig*>(user);
                ++self->begins;
                std::snprintf(self->last_stage, sizeof(self->last_stage), "%s", stage);
            },
            [](void* user, bool ok) {
                auto* self = static_cast<LossRig*>(user);
                (ok ? self->ends_ok : self->ends_failed) += 1;
            },
            this);
    }
    FrameStatus Render() { return renderer->RenderFrame(nullptr, nullptr, nullptr); }
};

bool LossBuild(const FrameContext& context, void* user) {
    auto* rig = static_cast<LossRig*>(user);
    if (rig->ask_during_frame) {
        rig->renderer->ApplySettings(rig->asked);
        rig->ask_during_frame = false;
    }
    rig->target = context.target;
    context.graph->AddPass("clear", &LossClear, &rig->target).Write(rig->target, GraphUsage::ColorAttachment);
    return true;
}

} // namespace

TEST_CASE("A device lost at the start of frame 3 is replaced and the next frame renders", "[recovery]") {
    LossQuietLog quiet;
    LossRig rig;
    rig.info.faults.lose_device_at_frame = 3;
    rig.Create(RecoveryPolicy{});
    // The first device has no fault; make the loss strike on the second device instead: lose it by hand-free means below.
    for (int i = 0; i < 2; ++i) CHECK(rig.Render() == FrameStatus::Ok);
    // Recreate once so the device that runs from now on carries the injected loss at its frame 3.
    RenderSettings wanted = rig.renderer->Settings();
    wanted.SetBool(kSyncValidation, true);
    REQUIRE(rig.renderer->ApplySettings(wanted).ok);
    const u32 epoch = rig.host.Epoch();
    CHECK(rig.Render() == FrameStatus::Ok);
    CHECK(rig.Render() == FrameStatus::Ok);
    CHECK(rig.Render() == FrameStatus::DeviceLost);   // the frame that found the loss
    CHECK(rig.host.Epoch() != epoch);                  // recovered at once
    CHECK_FALSE(rig.renderer->IsRecovering());
    CHECK(rig.renderer->Stats().recoveries == 1);
    CHECK(rig.renderer->Stats().frames_lost == 1);
    CHECK(rig.strikes.load() == 0);                           // the one allowed loss was used
    CHECK(rig.owner.recreates >= 1);
    CHECK(rig.Render() == FrameStatus::Ok);            // and rendering works again
    CHECK(rig.renderer->GetDevice() == rig.host.GetDevice());
    CHECK(rig.ends_ok >= 1);
}

TEST_CASE("Losses at submit and at present recover too", "[recovery]") {
    LossQuietLog quiet;
    SECTION("submit") {
        LossRig rig;
        rig.info.faults.lose_device_at_submit = 2;
        rig.Create(RecoveryPolicy{});
        RenderSettings wanted = rig.renderer->Settings();
        wanted.SetBool(kSyncValidation, true);
        REQUIRE(rig.renderer->ApplySettings(wanted).ok); // the new device carries the fault
        CHECK(rig.Render() == FrameStatus::Ok);
        CHECK(rig.Render() == FrameStatus::DeviceLost);
        CHECK(rig.renderer->Stats().recoveries == 1);
        CHECK(rig.Render() == FrameStatus::Ok);
    }
    SECTION("present") {
        LossRig rig;
        rig.info.faults.lose_device_at_present = 2;
        rig.Create(RecoveryPolicy{});
        RenderSettings wanted = rig.renderer->Settings();
        wanted.SetBool(kSyncValidation, true);
        REQUIRE(rig.renderer->ApplySettings(wanted).ok);
        CHECK(rig.Render() == FrameStatus::Ok);
        CHECK(rig.Render() == FrameStatus::DeviceLost);
        CHECK(rig.renderer->Stats().recoveries == 1);
        CHECK(rig.Render() == FrameStatus::Ok);
    }
}

TEST_CASE("A failed attempt waits and the next one recovers", "[recovery]") {
    LossQuietLog quiet;
    LossRig rig;
    rig.info.faults.lose_device_at_frame = 1;
    rig.Create(RecoveryPolicy{});
    RenderSettings wanted = rig.renderer->Settings();
    wanted.SetBool(kSyncValidation, true);
    REQUIRE(rig.renderer->ApplySettings(wanted).ok);
    rig.owner.fail_next = 1; // the first recovery attempt cannot rebuild its owner
    CHECK(rig.Render() == FrameStatus::DeviceLost);
    CHECK(rig.renderer->IsRecovering());
    CHECK(rig.renderer->Stats().recovery_failures == 1);
    CHECK(rig.Render() == FrameStatus::Skipped);       // waiting between attempts (one frame)
    CHECK(rig.renderer->IsRecovering());
    CHECK(rig.Render() == FrameStatus::Ok);            // attempt 2 worked inside this call and the frame rendered
    CHECK_FALSE(rig.renderer->IsRecovering());
    CHECK(rig.renderer->Stats().recoveries == 1);
}

TEST_CASE("After the retry limit the renderer stays device-lost with the reason", "[recovery]") {
    LossQuietLog quiet;
    LossRig rig;
    rig.info.faults.lose_device_at_frame = 1;
    RecoveryPolicy policy;
    policy.max_attempts = 3;
    policy.delay_frames = 1;
    rig.Create(policy);
    RenderSettings wanted = rig.renderer->Settings();
    wanted.SetBool(kSyncValidation, true);
    REQUIRE(rig.renderer->ApplySettings(wanted).ok);
    rig.owner.fail_next = 100;
    CHECK(rig.Render() == FrameStatus::DeviceLost);   // attempt 1 fails at once
    CHECK(rig.Render() == FrameStatus::Skipped);       // wait 1 frame
    CHECK(rig.Render() == FrameStatus::Skipped);       // attempt 2 fails, wait 2 frames
    CHECK(rig.Render() == FrameStatus::Skipped);
    CHECK(rig.Render() == FrameStatus::Skipped);
    CHECK(rig.Render() == FrameStatus::DeviceLost);    // attempt 3 fails: gives up
    CHECK(rig.renderer->IsBroken());
    CHECK(rig.renderer->Stats().recovery_failures == 3); // not a fourth attempt
    CHECK(rig.Render() == FrameStatus::DeviceLost);
    CHECK(rig.renderer->Stats().recovery_failures == 3);
    CHECK(rig.renderer->GetDevice() == nullptr);
}

TEST_CASE("With recovery off a loss is terminal as before", "[recovery]") {
    LossQuietLog quiet;
    LossRig rig;
    rig.info.faults.lose_device_at_frame = 1;
    RecoveryPolicy off;
    off.max_attempts = 0;
    rig.Create(off);
    RenderSettings wanted = rig.renderer->Settings();
    wanted.SetBool(kSyncValidation, true);
    REQUIRE(rig.renderer->ApplySettings(wanted).ok);
    CHECK(rig.Render() == FrameStatus::DeviceLost);
    CHECK(rig.Render() == FrameStatus::DeviceLost);
    CHECK(rig.renderer->Stats().recoveries == 0);
    CHECK_FALSE(rig.renderer->IsRecovering());
}

TEST_CASE("When the saved adapter keeps failing the recovery uses the best one", "[recovery]") {
    LossQuietLog quiet;
    NullAdaptersRestore restore;
    LossRig rig;
    rig.info.faults.lose_device_at_frame = 2;
    rig.Create(RecoveryPolicy{}, "null-integrated");
    RenderSettings wanted = rig.renderer->Settings();
    wanted.SetBool(kSyncValidation, true);
    REQUIRE(rig.renderer->ApplySettings(wanted).ok);
    CHECK(rig.Render() == FrameStatus::Ok);
    // The integrated adapter is gone (unsuitable) by the time the device is lost.
    rhi::AdapterList list;
    rhi::DefaultNullAdapters(&list);
    list.items[1].suitable = false;
    list.items[1].unsuitable_reason = "removed";
    rhi::SetNullAdapters(&list);
    CHECK(rig.Render() == FrameStatus::DeviceLost);   // attempt 1: the saved adapter, fails
    CHECK(rig.Render() == FrameStatus::Skipped);
    CHECK(rig.Render() == FrameStatus::Ok);            // attempt 2: the best adapter
    CHECK(std::strcmp(rig.host.GetDevice()->Adapter().id, "null-discrete") == 0);
    CHECK(rig.renderer->Settings().GetText(kAdapter)[0] == '\0');
    CHECK(rig.renderer->Stats().recoveries == 1);
}

TEST_CASE("A device lost twice in one run is recovered twice", "[recovery]") {
    LossQuietLog quiet;
    LossRig rig;
    rig.strikes = 2u;
    rig.info.faults.lose_device_at_frame = 2;
    rig.Create(RecoveryPolicy{});
    RenderSettings wanted = rig.renderer->Settings();
    wanted.SetBool(kSyncValidation, true);
    REQUIRE(rig.renderer->ApplySettings(wanted).ok);
    int lost = 0;
    for (int i = 0; i < 8; ++i) {
        if (rig.Render() == FrameStatus::DeviceLost) ++lost;
    }
    CHECK(lost == 2);
    CHECK(rig.renderer->Stats().recoveries == 2);
    CHECK_FALSE(rig.renderer->IsBroken());
    CHECK(rig.Render() == FrameStatus::Ok);
}

TEST_CASE("A settings change asked for during the lost frame is applied after the recovery", "[recovery]") {
    LossQuietLog quiet;
    LossRig rig;
    rig.info.faults.lose_device_at_submit = 1; // the loss comes after the build callback has run
    rig.Create(RecoveryPolicy{});
    RenderSettings wanted = rig.renderer->Settings();
    wanted.SetBool(kSyncValidation, true);
    REQUIRE(rig.renderer->ApplySettings(wanted).ok);
    rig.ask_during_frame = true;
    rig.asked = rig.renderer->Settings();
    rig.asked.SetEnum(kGBufferDebugLayer, 3);
    rig.asked.SetText(kAdapter, "null-integrated");
    CHECK(rig.Render() == FrameStatus::DeviceLost);   // the change was queued in this frame, then the device was lost
    CHECK(rig.Render() == FrameStatus::Ok);            // recovered; the queued change is applied at this boundary
    CHECK(std::strcmp(rig.host.GetDevice()->Adapter().id, "null-integrated") == 0);
    CHECK(rig.renderer->Settings().GetEnum(kGBufferDebugLayer) == 3);
}

TEST_CASE("A lost surface on a healthy device is replaced without a device reset", "[recovery]") {
    LossQuietLog quiet;
    LossRig rig;
    rig.strikes = 0;
    rig.Create(RecoveryPolicy{});
    CHECK(rig.Render() == FrameStatus::Ok);
    const u32 epoch = rig.host.Epoch();
    static_cast<rhi::NullDevice*>(rig.host.GetDevice())->SimulateSurfaceLoss();
    CHECK(rig.Render() == FrameStatus::DeviceLost);
    CHECK(rig.host.Epoch() == epoch);                  // same device
    CHECK(rig.host.SurfaceFallbackCount() == 0);
    CHECK(rig.renderer->Stats().recoveries == 1);
    CHECK(rig.Render() == FrameStatus::Ok);
}

TEST_CASE("A real device recovers from a loss at the start and at the submit of a frame", "[recovery][vulkan][gpu]") {
    LossQuietLog quiet;
    for (const char* where : {"frame", "submit"}) {
        INFO(where);
        DeviceResources registry;
        rhi::DeviceHost host;
        host.AddObserver(&registry, 0);
        std::atomic<u32> strikes{1};
        rhi::DeviceCreateInfo info;
        info.backend = rhi::Backend::Vulkan;
        info.validation = rhi::ValidationSetting::Off;
        if (!host.Create(info, rhi::SwapchainDesc{})) {
            SKIP("no suitable Vulkan device");
        }
        rhi::DeviceCreateInfo faulty = info;
        faulty.faults.strikes_left = &strikes;
        if (std::strcmp(where, "frame") == 0) faulty.faults.lose_device_at_frame = 2;
        else faulty.faults.lose_device_at_submit = 2;
        RenderSettings initial;
        initial.SetEnum(kBackend, 1);
        initial.SetEnum(kValidation, 1);
        struct Frame { GraphResource target; } frame;
        RendererDesc rd;
        rd.device = host.GetDevice();
        rd.host = &host;
        rd.create_info = faulty;
        rd.initial_settings = &initial;
        rd.resources = &registry;
        rd.build_user = &frame;
        rd.build = [](const FrameContext& context, void* user) {
            auto* f = static_cast<Frame*>(user);
            f->target = context.target;
            context.graph->AddPass("clear", &LossClear, &f->target).Write(f->target, GraphUsage::ColorAttachment);
            return true;
        };
        Renderer renderer(rd);
        // Move to a device that carries the fault.
        RenderSettings wanted = renderer.Settings();
        wanted.SetBool(kSyncValidation, true);
        REQUIRE(renderer.ApplySettings(wanted).ok);
        auto draw = [&]() {
            rhi::TextureDesc td;
            td.width = 32;
            td.height = 32;
            td.format = rhi::Format::RGBA8_UNorm;
            td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
            const rhi::TextureHandle target = renderer.GetDevice()->CreateTexture(td);
            FrameTarget ft;
            ft.texture = target;
            ft.width = 32;
            ft.height = 32;
            ft.format = rhi::Format::RGBA8_UNorm;
            ft.final_state = rhi::ResourceState::CopySrc;
            const FrameStatus status = renderer.RenderFrame(nullptr, nullptr, &ft);
            if (renderer.GetDevice() != nullptr) {
                renderer.GetDevice()->WaitIdle();
                // The target belongs to the device that drew it; after a recovery that device is gone, so only destroy it when
                // the device did not change during this frame.
            }
            return std::pair<FrameStatus, rhi::TextureHandle>{status, target};
        };
        auto first = draw();
        CHECK(first.first == FrameStatus::Ok);
        renderer.GetDevice()->DestroyTexture(first.second);
        // The second frame loses the device; the recovery makes a new one (the old target went with the old device).
        rhi::Device* before = renderer.GetDevice();
        auto second = draw();
        CHECK(second.first == FrameStatus::DeviceLost);
        CHECK(renderer.GetDevice() != nullptr);
        CHECK(renderer.Stats().recoveries == 1);
        (void)before;
        auto third = draw();
        CHECK(third.first == FrameStatus::Ok);
        renderer.GetDevice()->DestroyTexture(third.second);
        renderer.GetDevice()->WaitIdle();
        CHECK(renderer.GetDevice()->Resources().UserObjects() == 0);
    }
}

namespace {

// Resident memory in KiB from /proc/self/statm (0 where unavailable).
long ResidentKiB() {
    io::Path statm_path("/proc/self/statm", io::PathStyle::Posix);
    Vector<u8> bytes;
    if (io::ReadFile(statm_path, &bytes, 4096).Failed() || bytes.Size() == 0) return 0;
    char text[128] = {};
    std::memcpy(text, bytes.Data(), bytes.Size() < sizeof(text) - 1 ? bytes.Size() : sizeof(text) - 1);
    long pages = 0;
    long resident = 0;
    const int n = std::sscanf(text, "%ld %ld", &pages, &resident);
    return n == 2 ? resident * 4 : 0;
}

} // namespace

TEST_CASE("Repeated losses on a real device do not keep growing memory", "[recovery][vulkan][gpu]") {
    LossQuietLog quiet;
    DeviceResources registry;
    rhi::DeviceHost host;
    host.AddObserver(&registry, 0);
    std::atomic<u32> strikes{8};
    rhi::DeviceCreateInfo info;
    info.backend = rhi::Backend::Vulkan;
    info.validation = rhi::ValidationSetting::Off;
    if (!host.Create(info, rhi::SwapchainDesc{})) {
        SKIP("no suitable Vulkan device");
    }
    rhi::DeviceCreateInfo faulty = info;
    faulty.faults.strikes_left = &strikes;
    faulty.faults.lose_device_at_frame = 2;
    RenderSettings initial;
    initial.SetEnum(kBackend, 1);
    initial.SetEnum(kValidation, 1);
    struct Frame { GraphResource target; } frame;
    RendererDesc rd;
    rd.device = host.GetDevice();
    rd.host = &host;
    rd.create_info = faulty;
    rd.initial_settings = &initial;
    rd.resources = &registry;
    rd.build_user = &frame;
    rd.build = [](const FrameContext& context, void* user) {
        auto* f = static_cast<Frame*>(user);
        f->target = context.target;
        context.graph->AddPass("clear", &LossClear, &f->target).Write(f->target, GraphUsage::ColorAttachment);
        return true;
    };
    Renderer renderer(rd);
    RenderSettings wanted = renderer.Settings();
    wanted.SetBool(kSyncValidation, true);
    REQUIRE(renderer.ApplySettings(wanted).ok);
    auto frame_once = [&]() {
        rhi::TextureDesc td;
        td.width = 32;
        td.height = 32;
        td.format = rhi::Format::RGBA8_UNorm;
        td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::CopySrc;
        rhi::Device* device = renderer.GetDevice();
        const u32 epoch = host.Epoch(); // (a new device can get the old one's address, so compare serials)
        const rhi::TextureHandle target = device->CreateTexture(td);
        FrameTarget ft;
        ft.texture = target;
        ft.width = 32;
        ft.height = 32;
        ft.format = rhi::Format::RGBA8_UNorm;
        ft.final_state = rhi::ResourceState::CopySrc;
        const FrameStatus status = renderer.RenderFrame(nullptr, nullptr, &ft);
        if (host.Epoch() == epoch) {
            device->WaitIdle();
            device->DestroyTexture(target);
        } // otherwise the device (and the target with it) was replaced
        return status;
    };
    long after_first = 0;
    int losses = 0;
    for (int i = 0; i < 40 && losses < 8; ++i) {
        if (frame_once() == FrameStatus::DeviceLost) {
            ++losses;
            if (losses == 2) after_first = ResidentKiB();
        }
    }
    const long after_last = ResidentKiB();
    WARN("losses " << losses << ", resident " << after_first << " KiB after the 2nd, " << after_last << " KiB after the last");
    CHECK(losses == 8);
    CHECK(renderer.Stats().recoveries == 8);
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define LOSS_TEST_SANITIZED 1
#endif
#endif
#ifndef LOSS_TEST_SANITIZED // the sanitizer's quarantine makes resident memory meaningless
    if (after_first != 0) {
        CHECK(after_last - after_first < 40 * 1024); // 6 more losses must not cost 40 MiB
    }
#endif
}

TEST_CASE("A request queued while recovering does not bring back the abandoned adapter", "[recovery]") {
    LossQuietLog quiet;
    NullAdaptersRestore restore;
    LossRig rig;
    rig.info.faults.lose_device_at_frame = 2;
    rig.Create(RecoveryPolicy{}, "null-integrated");
    RenderSettings wanted = rig.renderer->Settings();
    wanted.SetBool(kSyncValidation, true);
    REQUIRE(rig.renderer->ApplySettings(wanted).ok);
    CHECK(rig.Render() == FrameStatus::Ok);
    rhi::AdapterList list;
    rhi::DefaultNullAdapters(&list);
    list.items[1].suitable = false;
    list.items[1].unsuitable_reason = "removed";
    rhi::SetNullAdapters(&list);
    CHECK(rig.Render() == FrameStatus::DeviceLost);   // attempt 1 fails (the saved adapter)
    RenderSettings stale = rig.renderer->Settings();  // still names the saved adapter
    stale.SetEnum(kGBufferDebugLayer, 2);
    CHECK(rig.renderer->ApplySettings(stale).queued);
    CHECK(rig.Render() == FrameStatus::Skipped);
    CHECK(rig.Render() == FrameStatus::Ok);            // attempt 2 on the best adapter, then the queued change
    CHECK(std::strcmp(rig.host.GetDevice()->Adapter().id, "null-discrete") == 0);
    CHECK(rig.renderer->Settings().GetText(kAdapter)[0] == '\0');
    CHECK(rig.renderer->Settings().GetEnum(kGBufferDebugLayer) == 2);
    CHECK(rig.renderer->LastApply().performed == CostClass::Dynamic); // no reset back to the failing adapter
}

TEST_CASE("A device told to be lost (as the demo's F10 key does) is recovered from", "[recovery]") {
    LossQuietLog quiet;
    LossRig rig;
    rig.strikes = 0;
    rig.Create(RecoveryPolicy{});
    CHECK(rig.Render() == FrameStatus::Ok);
    const u32 epoch = rig.host.Epoch();
    rig.renderer->GetDevice()->InjectDeviceLoss();
    CHECK(rig.Render() == FrameStatus::DeviceLost);
    CHECK(rig.host.Epoch() != epoch);
    CHECK(rig.renderer->Stats().recoveries == 1);
    CHECK(rig.Render() == FrameStatus::Ok);
}
