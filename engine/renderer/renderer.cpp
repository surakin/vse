#include "renderer/renderer.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>

#include "debug/crash_state.h"
#include "log/assert.h"
#include "os/os.h"
#include "renderer/renderer_log.h"

namespace engine_placeholder::renderer {

const char* FrameStatusName(FrameStatus status) {
    switch (status) {
    case FrameStatus::Ok: return "ok";
    case FrameStatus::Skipped: return "skipped";
    case FrameStatus::DeviceLost: return "device-lost";
    case FrameStatus::Failed: return "failed";
    }
    return "?";
}

namespace {

// Checked before any member is built from the description.
const RendererDesc& Checked(const RendererDesc& desc) {
    ENGINE_ASSERT(desc.device != nullptr, "Renderer: the device is null");
    ENGINE_ASSERT(desc.build != nullptr, "Renderer: the frame callback is null");
    return desc;
}

} // namespace

Renderer::Renderer(const RendererDesc& desc)
    : desc_(Checked(desc)), settings_(), pending_(), arena_(desc.arena_bytes), pool_(desc.device, desc.pool) {
    if (desc.initial_settings != nullptr) {
        settings_ = *desc.initial_settings;
    }
    if (!pool_.RegisterWith(desc.resources)) {
        RenderLog(LogVerbosity::Error, "Renderer: the transient pool could not register with the device resources");
    }
    if (desc.initial_settings == nullptr) {
        // The host's device was made from create_info: the settings in force must say so, or the next reset would switch
        // backend, adapter or validation to the defaults.
        settings_.SetEnum(settings::kBackend, desc.create_info.backend == rhi::Backend::Null ? 0 : 1);
        settings_.SetEnum(settings::kValidation, desc.create_info.validation == rhi::ValidationSetting::Off ? 1 : desc.create_info.validation == rhi::ValidationSetting::On ? 2 : 0);
        settings_.SetBool(settings::kSyncValidation, desc.create_info.sync_validation);
        settings_.SetInt(settings::kFramesInFlight, desc.create_info.frames_in_flight);
        if (desc.create_info.adapter != nullptr) {
            settings_.SetText(settings::kAdapter, desc.create_info.adapter);
        }
    }
    active_info_ = desc.create_info;
    BuildCreateInfo(settings_, active_adapter_, &active_info_);
    RefreshCapabilities(desc.host != nullptr); // the adapter list is only read when devices can be replaced
}

void Renderer::Rebind(rhi::Device* device, rhi::Swapchain* swapchain) {
    ENGINE_ASSERT(device != nullptr, "Renderer::Rebind: the device is null");
    ENGINE_ASSERT(desc_.resources != nullptr, "Renderer::Rebind: the renderer was built without a DeviceResources registry, so its pool cannot follow the device");
    desc_.device = device;
    desc_.swapchain = swapchain;
}

namespace {

f64 NowMs() { return static_cast<f64>(os::MonotonicTimeNanos()) / 1.0e6; }

void SetReason(ApplyResult* r, const char* text) { std::snprintf(r->reason, sizeof(r->reason), "%s", text); }

} // namespace

void MakeDeviceCreateInfo(const RenderSettings& settings, const rhi::DeviceCreateInfo& base, char* adapter_buffer, rhi::DeviceCreateInfo* out) {
    *out = base;
    out->backend = settings.GetEnum(settings::kBackend) == 0 ? rhi::Backend::Null : rhi::Backend::Vulkan;
    const char* adapter = settings.GetText(settings::kAdapter);
    if (adapter != nullptr && adapter[0] != '\0') {
        std::snprintf(adapter_buffer, 64, "%s", adapter);
        out->adapter = adapter_buffer;
    } else {
        adapter_buffer[0] = '\0';
        out->adapter = nullptr;
    }
    const u32 validation = settings.GetEnum(settings::kValidation);
    out->validation = validation == 1 ? rhi::ValidationSetting::Off : validation == 2 ? rhi::ValidationSetting::On : rhi::ValidationSetting::Auto;
    out->sync_validation = settings.GetBool(settings::kSyncValidation);
    out->frames_in_flight = static_cast<u32>(settings.GetInt(settings::kFramesInFlight));
}

rhi::SwapchainDesc MakeSwapchainDesc(const RenderSettings& settings) {
    rhi::SwapchainDesc d;
    const u32 mode = settings.GetEnum(settings::kPresentMode);
    d.present_mode = mode == 1 ? rhi::PresentMode::Mailbox : mode == 2 ? rhi::PresentMode::Immediate : rhi::PresentMode::Fifo;
    d.width = static_cast<u32>(settings.GetInt(settings::kWidth));
    d.height = static_cast<u32>(settings.GetInt(settings::kHeight));
    const u32 format = settings.GetEnum(settings::kSwapchainFormat);
    d.format = format == 1 ? rhi::Format::BGRA8_SRGB : format == 2 ? rhi::Format::RGBA16_Float : rhi::Format::Undefined;
    const u32 hdr = settings.GetEnum(settings::kHdr);
    d.hdr = hdr == 1 ? rhi::HdrMode::ScRgbLinear : hdr == 2 ? rhi::HdrMode::Hdr10 : rhi::HdrMode::Off;
    return d;
}

void Renderer::RefreshCapabilities(bool adapters_too) {
    const u32 adapter_count = caps_.AdapterCount();
    caps_ = Capabilities();
    if (desc_.window != nullptr && desc_.window->SupportsFullscreen()) {
        caps_.Add("fullscreen");
    }
    if (desc_.swapchain != nullptr && desc_.swapchain->Capabilities().hdr_scrgb) {
        caps_.Add("hdr");
    }
    u32 suitable = adapter_count; // only a device reset can change the adapter list
    if (adapters_too) {
        rhi::AdapterList adapters;
        suitable = 0;
        if (rhi::EnumerateAdapters(active_info_, &adapters)) {
            for (u32 i = 0; i < adapters.count; ++i) {
                suitable += adapters.items[i].suitable ? 1 : 0;
            }
        }
    }
    caps_.SetAdapterCount(suitable);
}

ApplyResult Renderer::ApplySettings(const RenderSettings& requested) {
    if (in_frame_ || recovering_) {
        pending_ = requested; // a newer request replaces an older one
        has_pending_ = true;
        ApplyResult queued;
        queued.queued = true;
        queued.ok = true;
        return queued;
    }
    has_pending_ = false; // a newer request replaces a queued one
    last_apply_ = ApplyNow(requested);
    return last_apply_;
}

// Puts the previous device back after a failed replacement. Returns whether it worked.
bool Renderer::RollbackDevice(ApplyResult* result) {
    rhi::DeviceHost* host = desc_.host;
    char adapter[64];
    rhi::DeviceCreateInfo info;
    BuildCreateInfo(settings_, adapter, &info);
    const rhi::SwapchainDesc sc = BuildSwapchainDesc(settings_);
    host->Destroy();
    if (!host->Create(info, sc)) {
        SetReason(result, host->LastError()[0] != '\0' ? host->LastError() : "the previous device could not be created again");
        return false;
    }
    std::snprintf(active_adapter_, sizeof(active_adapter_), "%s", adapter);
    active_info_ = info;
    active_info_.adapter = active_adapter_[0] != '\0' ? active_adapter_ : nullptr;
    desc_.device = host->GetDevice();
    desc_.swapchain = host->GetSwapchain();
    return true;
}

ApplyResult Renderer::ForceDeviceReset() {
    if (in_frame_ || recovering_) {
        ApplyResult refused;
        SetReason(&refused, "a reset cannot be forced inside a frame or while recovering");
        return refused;
    }
    last_apply_ = ApplyNow(settings_, /*force_device_reset=*/true); // a queued request stays queued and applies at the next boundary
    return last_apply_;
}

ApplyResult Renderer::ApplyNow(const RenderSettings& requested, bool force_device_reset) {
    ApplyResult result;
    const f64 start = NowMs();
    if (broken_) {
        SetReason(&result, "the renderer has no device (an earlier settings change failed and could not be undone)");
        return result;
    }
    result.change = DiffSettings(settings_, requested, caps_); // also lists what needs a missing capability
    RenderSettings resolved = ResolveSettings(settings_, requested, caps_);
    resolved.SetGeneration(settings_.Generation() + 1); // changes only when the change is applied
    if (!result.change.Any() && !force_device_reset) {
        result.ok = true;
        return result;
    }
    CostClass cost = force_device_reset ? CostClass::DeviceReset : result.change.cost;
    rhi::DeviceHost* host = desc_.host;
    if (cost > CostClass::Dynamic && host == nullptr) {
        SetReason(&result, "this settings change needs a DeviceHost and the renderer was built without one");
        return result;
    }
    if (cost > CostClass::Dynamic && host->GetDevice() == nullptr) {
        SetReason(&result, "the renderer has no device right now (a device loss is being recovered from)");
        return result;
    }
    if (cost == CostClass::PipelineRebuild && desc_.resources == nullptr) {
        cost = CostClass::DeviceReset; // nothing registered to rebuild selectively: do the full thing
    }
    result.performed = cost;

    if (cost <= CostClass::Dynamic) {
        settings_ = resolved;
    } else {
        if (reset_begin_ != nullptr) {
            reset_begin_(reset_user_, cost == CostClass::Swapchain ? "swapchain" : cost == CostClass::PipelineRebuild ? "pipelines" : "device");
        }
        bool ok = true;
        bool start_recovery = false;
        host->GetDevice()->WaitIdle(); // frames in flight drained; the build callback is not running
        if (cost == CostClass::Swapchain) {
            f64 t = NowMs();
            const bool fullscreen_changed = resolved.GetBool(settings::kFullscreen) != settings_.GetBool(settings::kFullscreen) && desc_.window != nullptr;
            if (fullscreen_changed) {
                desc_.window->SetFullscreen(resolved.GetBool(settings::kFullscreen));
            }
            // A device with no swapchain (offscreen) has nothing to reconfigure: the values are just stored.
            const rhi::SwapchainStatus status =
                host->GetSwapchain() != nullptr ? host->ReconfigureSwapchain(BuildSwapchainDesc(resolved)) : rhi::SwapchainStatus::Ok;
            result.swapchain_ms = NowMs() - t;
            if (status == rhi::SwapchainStatus::Lost) {
                ok = false;
                SetReason(&result, "the swapchain could not be reconfigured");
                if (fullscreen_changed) {
                    desc_.window->SetFullscreen(settings_.GetBool(settings::kFullscreen)); // the window goes back too
                }
                if (host->ReconfigureSwapchain(BuildSwapchainDesc(settings_)) == rhi::SwapchainStatus::Lost) {
                    result.device_lost = true;
                } else {
                    result.rolled_back = true;
                }
            }
        } else if (cost == CostClass::PipelineRebuild) {
            f64 t = NowMs();
            desc_.resources->ReleaseFrom(tiers::kMaterials);
            result.release_ms = NowMs() - t;
            t = NowMs();
            if (!desc_.resources->RecreateFrom(tiers::kMaterials)) {
                ok = false;
                SetReason(&result, "an owner could not be rebuilt");
                result.rebuild_ms = NowMs() - t;
                // Try the same rebuild once more (the settings it read have not changed); if that fails the device is unusable.
                result.rolled_back = desc_.resources->RecreateFrom(tiers::kMaterials);
                result.device_lost = !result.rolled_back;
            } else {
                result.rebuild_ms = NowMs() - t;
            }
        } else {
            char adapter[64];
            rhi::DeviceCreateInfo info;
            BuildCreateInfo(resolved, adapter, &info);
            const rhi::SwapchainDesc sc = BuildSwapchainDesc(resolved);
            f64 t = NowMs();
            ok = host->Recreate(info, sc);
            result.device_ms = NowMs() - t;
            result.reset = host->LastTimings();
            if (ok) {
                std::snprintf(active_adapter_, sizeof(active_adapter_), "%s", adapter);
                active_info_ = info;
                active_info_.adapter = active_adapter_[0] != '\0' ? active_adapter_ : nullptr;
                desc_.device = host->GetDevice();
                desc_.swapchain = host->GetSwapchain();
                ++stats_.resets;
            } else {
                SetReason(&result, host->LastError()[0] != '\0' ? host->LastError() : "the device could not be replaced");
                if (RollbackDevice(&result)) {
                    result.rolled_back = true;
                } else {
                    result.device_lost = true;
                }
            }
        }
        if (ok) {
            if (host->GetDevice() != nullptr) {
                desc_.device = host->GetDevice();
                desc_.swapchain = host->GetSwapchain();
            }
            settings_ = resolved;
        } else {
            if (result.rolled_back) {
                ++stats_.rollbacks;
            }
            if (result.device_lost) {
                desc_.device = nullptr; // nothing valid to hand out
                desc_.swapchain = nullptr;
                if (desc_.recovery.max_attempts > 0) {
                    force_full_recovery_ = true; // the device is not "lost" to the driver, but the owners must all be rebuilt
                    start_recovery = true;       // after the end callback, so the callbacks stay balanced
                } else {
                    broken_ = true;
                }
            }
        }
        if (reset_end_ != nullptr) {
            reset_end_(reset_user_, ok);
        }
        if (start_recovery) {
            NoteLost(); // tries again by the recovery policy; gives up (broken) after the last attempt
        }
        result.ok = ok;
        if (ok) {
            RefreshCapabilities(cost == CostClass::DeviceReset);
        }
    }
    if (cost <= CostClass::Dynamic) {
        result.ok = true;
    }
    if (result.ok) {
        ++stats_.applies[static_cast<usize>(result.performed)];
        static std::atomic<u64>* const generation = debug::CrashStateCounter("settings_generation");
        if (generation != nullptr) {
            generation->store(settings_.Generation(), std::memory_order_relaxed);
        }
    }
    result.total_ms = NowMs() - start;
    if (cost == CostClass::DeviceReset) {
        stats_.last_reset_ms = result.total_ms;
    }
    return result;
}

void Renderer::SetError(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(error_, sizeof(error_), fmt, ap);
    va_end(ap);
}

// Ends a frame that did not render. With an acquired swapchain image the image still has to be presented, or the next
// Acquire would find the previous one outstanding; an image that was never drawn is moved to Present and shown as it is.
FrameStatus Renderer::Abandon(FrameStatus status, bool acquired) {
    rhi::Device* device = desc_.device;
    if (acquired && !device->DeviceLost()) {
        rhi::CommandList* list = device->CreateCommandList();
        if (list != nullptr) {
            list->Begin();
            list->Barrier(desc_.swapchain->CurrentTexture(), rhi::ResourceState::Undefined, rhi::ResourceState::Present);
            list->End();
            rhi::CommandList* lists[] = {list};
            device->Submit(lists, 1);
        }
    }
    pool_.EndFrame();
    device->EndFrame();
    if (acquired) {
        const rhi::SwapchainStatus presented = desc_.swapchain->Present();
        if (presented == rhi::SwapchainStatus::Lost) status = FrameStatus::DeviceLost;
        else if (presented == rhi::SwapchainStatus::Recreated) ++stats_.swapchain_recreations;
    }
    if (device->DeviceLost()) status = FrameStatus::DeviceLost;
    if (status == FrameStatus::Failed) ++stats_.frames_failed;
    else if (status == FrameStatus::DeviceLost) ++stats_.frames_lost;
    return status;
}

FrameStatus Renderer::RenderFrame(const RenderScene* scene, const Camera* camera, const FrameTarget* offscreen) {
    error_[0] = 0;
    if (broken_) {
        SetError("the renderer has no device");
        ++stats_.frames_lost;
        return FrameStatus::DeviceLost;
    }
    if (recovering_) {
        if (recovery_wait_ > 0) {
            --recovery_wait_;
            SetError("recovering from a device loss (attempt %u of %u)", recovery_attempt_ + 1, desc_.recovery.max_attempts);
            ++stats_.frames_skipped;
            return FrameStatus::Skipped;
        }
        if (!RecoveryAttempt()) {
            SetError("recovering from a device loss failed (attempt %u of %u)", recovery_attempt_, desc_.recovery.max_attempts);
            if (broken_) {
                ++stats_.frames_lost;
                return FrameStatus::DeviceLost;
            }
            ++stats_.frames_skipped;
            return FrameStatus::Skipped;
        }
    }
    if (has_pending_) {
        // A settings change asked for during a frame is applied at the boundary, before anything of this frame exists.
        has_pending_ = false;
        last_apply_ = ApplyNow(pending_);
        if (!last_apply_.ok) {
            SetError("settings change failed: %s", last_apply_.reason);
            if (broken_) {
                ++stats_.frames_lost;
                return FrameStatus::DeviceLost;
            }
            ++stats_.frames_failed;
            return FrameStatus::Failed;
        }
    }
    in_frame_ = true;
    const FrameStatus status = RenderFrameImpl(scene, camera, offscreen);
    in_frame_ = false;
    if (status == FrameStatus::DeviceLost) {
        NoteLost();
    }
    return status;
}

// A frame ended DeviceLost: start recovering (the first attempt is made now).
void Renderer::NoteLost() {
    if (desc_.host == nullptr || desc_.recovery.max_attempts == 0 || recovering_ || broken_) {
        return;
    }
    recovering_ = true;
    recovery_attempt_ = 0;
    recovery_wait_ = 0;
    RenderLog(LogVerbosity::Warn, "Renderer: the device was lost; recovering (up to %u attempts)", desc_.recovery.max_attempts);
    debug::SetCrashStateText("rhi_recovery", "recovering");
    RecoveryAttempt();
}

// One attempt to get a working device back. Returns true (and ends recovery) on success. On failure schedules the next attempt
// or, after the last, gives up: the renderer stays device-lost.
bool Renderer::RecoveryAttempt() {
    rhi::DeviceHost* host = desc_.host;
    ++recovery_attempt_;
    char note[64];
    std::snprintf(note, sizeof(note), "recovering (attempt %u)", recovery_attempt_);
    debug::SetCrashStateText("rhi_recovery", note);
    RenderLog(LogVerbosity::Warn, "Renderer: recovery attempt %u of %u (adapter '%s')", recovery_attempt_, desc_.recovery.max_attempts,
              settings_.GetText(settings::kAdapter));
    if (reset_begin_ != nullptr) {
        reset_begin_(reset_user_, "recovery");
    }
    RenderSettings attempt_settings = settings_;
    bool fell_back = false;
    if (recovery_attempt_ >= 2 && desc_.recovery.fallback_adapter && settings_.GetText(settings::kAdapter)[0] != '\0') {
        attempt_settings.SetText(settings::kAdapter, ""); // the saved adapter keeps failing: take the best one
        fell_back = true;
    }
    bool ok = false;
    bool surface_only = false;
    // A lost surface on a healthy device is repaired without a device reset.
    if (!force_full_recovery_ && host->GetDevice() != nullptr && !host->GetDevice()->DeviceLost() && host->GetSwapchain() != nullptr) {
        const u32 epoch = host->Epoch();
        ok = host->RecreateSurface(desc_.window, BuildSwapchainDesc(attempt_settings));
        surface_only = ok && host->Epoch() == epoch;
    } else {
        char adapter[64];
        rhi::DeviceCreateInfo info;
        BuildCreateInfo(attempt_settings, adapter, &info);
        ok = host->Recreate(info, BuildSwapchainDesc(attempt_settings));
        if (ok) {
            std::snprintf(active_adapter_, sizeof(active_adapter_), "%s", adapter);
            active_info_ = info;
            active_info_.adapter = active_adapter_[0] != '\0' ? active_adapter_ : nullptr;
        }
    }
    desc_.device = host->GetDevice();
    desc_.swapchain = host->GetSwapchain();
    if (reset_end_ != nullptr) {
        reset_end_(reset_user_, ok);
    }
    if (ok) {
        if (fell_back) {
            // A request queued meanwhile was built from the old adapter: it must not bring the abandoned adapter back.
            if (has_pending_ && std::strcmp(pending_.GetText(settings::kAdapter), settings_.GetText(settings::kAdapter)) == 0) {
                pending_.SetText(settings::kAdapter, "");
            }
            RenderSettings kept = attempt_settings;
            kept.SetGeneration(settings_.Generation() + 1);
            settings_ = kept;
            static std::atomic<u64>* const generation = debug::CrashStateCounter("settings_generation");
            if (generation != nullptr) generation->store(settings_.Generation(), std::memory_order_relaxed);
        }
        recovering_ = false;
        force_full_recovery_ = false;
        ++stats_.recoveries;
        if (!surface_only) {
            ++stats_.resets;
            RefreshCapabilities(true);
        } else {
            RefreshCapabilities(false);
        }
        debug::SetCrashStateText("rhi_recovery", "none");
        RenderLog(LogVerbosity::Warn, "Renderer: recovered after %u attempt(s)", recovery_attempt_);
        return true;
    }
    ++stats_.recovery_failures;
    if (recovery_attempt_ >= desc_.recovery.max_attempts) {
        recovering_ = false;
        force_full_recovery_ = false;
        broken_ = true;
        desc_.device = nullptr;
        desc_.swapchain = nullptr;
        debug::SetCrashStateText("rhi_recovery", "gave up");
        RenderLog(LogVerbosity::Error, "Renderer: giving up after %u recovery attempt(s): %s", recovery_attempt_, host->LastError());
        return false;
    }
    u32 delay = desc_.recovery.delay_frames;
    for (u32 i = 1; i < recovery_attempt_ && delay < desc_.recovery.max_delay_frames; ++i) delay *= 2;
    recovery_wait_ = delay < desc_.recovery.max_delay_frames ? delay : desc_.recovery.max_delay_frames;
    return false;
}

FrameStatus Renderer::RenderFrameImpl(const RenderScene* scene, const Camera* camera, const FrameTarget* offscreen) {
    rhi::Device* device = desc_.device;
    error_[0] = 0;
    if (desc_.swapchain == nullptr) {
        ENGINE_ASSERT(offscreen != nullptr && offscreen->texture.IsValid(), "Renderer::RenderFrame: no swapchain and no offscreen target");
    }
    ++frame_;
    device->BeginFrame();
    pool_.BeginFrame(frame_);
    if (device->DeviceLost()) {
        SetError("the device is lost");
        return Abandon(FrameStatus::DeviceLost, false);
    }

    FrameTarget target;
    bool acquired = false;
    if (desc_.swapchain != nullptr) {
        const rhi::SwapchainStatus status = desc_.swapchain->Acquire();
        if (status == rhi::SwapchainStatus::Lost) {
            SetError("the swapchain is lost");
            return Abandon(FrameStatus::DeviceLost, false);
        }
        if (status == rhi::SwapchainStatus::Minimized) {
            ++stats_.frames_skipped;
            Abandon(FrameStatus::Skipped, false);
            return FrameStatus::Skipped;
        }
        if (status == rhi::SwapchainStatus::Recreated) ++stats_.swapchain_recreations;
        acquired = true;
        target.texture = desc_.swapchain->CurrentTexture();
        target.width = desc_.swapchain->Width();
        target.height = desc_.swapchain->Height();
        target.format = desc_.swapchain->ImageFormat();
        target.initial = rhi::ResourceState::Undefined;
        target.final_state = rhi::ResourceState::Present;
    } else {
        target = *offscreen;
    }

    arena_.Reset();
    RenderGraph graph(&arena_, desc_.graph);
    if (!graph.Valid()) {
        SetError("the frame arena (%llu bytes) is too small for the graph's tables", static_cast<unsigned long long>(desc_.arena_bytes));
        return Abandon(FrameStatus::Failed, acquired);
    }
    FrameContext context;
    context.graph = &graph;
    context.target = graph.ImportTexture("target", target.texture, target.initial, target.final_state, false);
    context.width = target.width;
    context.height = target.height;
    context.format = target.format;
    context.scene = scene;
    context.camera = camera;
    context.device = device;
    context.frame_index = frame_;
    {
        // The crash report shows which frame was being built.
        static std::atomic<u64>* const frame_counter = debug::CrashStateCounter("frame");
        if (frame_counter != nullptr) {
            frame_counter->store(frame_, std::memory_order_relaxed);
        }
    }
    if (!desc_.build(context, desc_.build_user)) {
        SetError("the frame callback abandoned the frame");
        return Abandon(FrameStatus::Failed, acquired);
    }
    if (!graph.Compile()) {
        SetError("the graph did not compile: %s", graph.ErrorCount() > 0 ? graph.Error(0) : "(no message)");
        return Abandon(FrameStatus::Failed, acquired);
    }
    rhi::FenceValue fence = 0;
    if (!graph.ExecuteParallel(&pool_, device, desc_.scheduler, &fence)) {
        SetError("%s", graph.ExecuteError());
        return Abandon(device->DeviceLost() ? FrameStatus::DeviceLost : FrameStatus::Failed, acquired);
    }
    stats_.last_fence = fence;
    pool_.EndFrame();
    device->EndFrame();
    FrameStatus result = FrameStatus::Ok;
    if (acquired) {
        const rhi::SwapchainStatus presented = desc_.swapchain->Present();
        if (presented == rhi::SwapchainStatus::Lost) result = FrameStatus::DeviceLost;
        else if (presented == rhi::SwapchainStatus::Recreated) ++stats_.swapchain_recreations;
    }
    if (device->DeviceLost()) result = FrameStatus::DeviceLost;
    if (result == FrameStatus::Ok) ++stats_.frames_rendered;
    else ++stats_.frames_lost;
    return result;
}

} // namespace engine_placeholder::renderer
