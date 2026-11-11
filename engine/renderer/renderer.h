#pragma once

//! The renderer's frame loop: begins a frame, builds and executes the render graph, and presents.
//!
//! A [[renderer::Renderer]] owns what lives across frames (the transient pool, the per-frame arena) and borrows the
//! device, the optional swapchain and the optional job scheduler. A frame is built by a callback that declares the passes
//! on a fresh [[renderer::RenderGraph]]; the renderer then compiles it, records the passes (in parallel when it has a
//! scheduler), submits once and presents. Later days supply the real callback (G-buffer, lighting, tonemap); tests supply
//! their own.

#include "debug/compiler.h"
#include "mem/arena_allocator.h"
#include "renderer/camera.h"
#include "renderer/render_graph.h"
#include "renderer/render_scene.h"
#include "renderer/render_settings.h"
#include "renderer/transient_pool.h"
#include "rhi/device_host.h"
#include "rhi/rhi.h"
#include "rhi/swapchain.h"
#include "types.h"

namespace engine_placeholder::jobs {
class Scheduler;
} // namespace engine_placeholder::jobs

namespace engine_placeholder::renderer {

/// How a frame ended.
enum class FrameStatus : u8 {
    Ok = 0,     ///< The frame was recorded, submitted and (with a swapchain) presented.
    Skipped,    ///< Nothing to draw to (the window is minimized); no work was submitted. Try again next frame.
    DeviceLost, ///< The device or the swapchain surface is lost. The renderer stays destroyable but cannot render again.
    Failed,     ///< The frame could not be built or recorded (a bad graph, an allocation failure); see `LastError`. Later frames may succeed.
};

/// Returns a status's name (`ok`, `skipped`, `device-lost`, `failed`).
///
/// - Param status: any status.
/// - Returns: a static string.
/// - Fails: never.
/// - Ownership: the string is static and never freed.
const char* FrameStatusName(FrameStatus status);

/// The texture a frame renders to when there is no swapchain.
///
/// - Thread-safety: thread-compatible plain data.
struct FrameTarget {
    /// The texture; must be valid and created with `ColorAttachment` usage.
    rhi::TextureHandle texture;
    /// Its width in pixels.
    u32 width = 0;
    /// Its height in pixels.
    u32 height = 0;
    /// Its format.
    rhi::Format format = rhi::Format::RGBA8_UNorm;
    /// The state it is in when the frame starts.
    rhi::ResourceState initial = rhi::ResourceState::Undefined;
    /// The state the frame must leave it in (for example `CopySrc` to read it back).
    rhi::ResourceState final_state = rhi::ResourceState::CopySrc;
};

/// What the frame callback is given.
///
/// - Thread-safety: thread-compatible plain data; valid only during the callback.
struct FrameContext {
    /// The graph to declare the passes on; it is empty except for the imported target.
    RenderGraph* graph = nullptr;
    /// The imported render target (the swapchain image or the offscreen texture).
    GraphResource target;
    /// The target's width in pixels.
    u32 width = 0;
    /// The target's height in pixels.
    u32 height = 0;
    /// The target's format.
    rhi::Format format = rhi::Format::RGBA8_UNorm;
    /// The scene to draw; may be null.
    const RenderScene* scene = nullptr;
    /// The camera; may be null.
    const Camera* camera = nullptr;
    /// The device, for creating per-frame objects the passes need.
    rhi::Device* device = nullptr;
    /// The frame number, counted from 1 by the renderer.
    u64 frame_index = 0;
};

/// A frame callback: declare the passes on `context.graph`. Return false to abandon the frame.
using FrameBuildFn = bool (*)(const FrameContext& context, void* user);

/// What the renderer does when the device (or its swapchain surface) is lost.
///
/// Needs a [[renderer::RendererDesc::host]]; without one a loss stays terminal. Attempts and delays are counted in frames, not
/// seconds, so tests are deterministic.
///
/// - Thread-safety: thread-compatible plain data.
struct RecoveryPolicy {
    /// The most reset attempts for one loss; 0 turns recovery off (the loss is terminal, as before Phase 8).
    u32 max_attempts = 3;
    /// Frames to wait before the second attempt; each later attempt waits twice as long as the one before, up to `max_delay_frames`.
    /// The first attempt is made at once, in the frame that found the loss.
    u32 delay_frames = 1;
    /// The longest wait between attempts, in frames.
    u32 max_delay_frames = 8;
    /// Whether the second and later attempts may use the best adapter instead of the saved one when the saved one keeps failing.
    bool fallback_adapter = true;
};

/// What a [[renderer::Renderer]] is made of.
///
/// - Thread-safety: thread-compatible plain data.
struct RendererDesc {
    /// The device; must not be null.
    rhi::Device* device = nullptr;
    /// The swapchain frames render to and present, or null to render to a [[renderer::FrameTarget]] each frame.
    rhi::Swapchain* swapchain = nullptr;
    /// Runs the recording in parallel, or null to record on the calling thread.
    jobs::Scheduler* scheduler = nullptr;
    /// Size of the per-frame arena that holds the graph; a frame that needs more fails.
    usize arena_bytes = 4u << 20;
    /// Capacities of each frame's graph.
    RenderGraphDesc graph;
    /// Capacities and aging of the transient pool.
    TransientPoolDesc pool;
    /// The device host the renderer takes its device and swapchain from and replaces them through (for `ApplySettings`), or null
    /// for the borrowed-device path in which only `None` and `Dynamic` settings can be applied. When set, `device` and
    /// `swapchain` must be the host's current ones; the renderer follows the host from then on. Borrowed; must outlive the renderer.
    rhi::DeviceHost* host = nullptr;
    /// What every device is created with besides the settings (the window, application name, pipeline cache). Backend, adapter,
    /// validation, sync validation and frames in flight come from the settings. `window` and the strings must outlive the renderer.
    rhi::DeviceCreateInfo create_info;
    /// What to do when the device is lost.
    RecoveryPolicy recovery;
    /// The window the swapchain presents to, for fullscreen and the capabilities; null for none. Borrowed.
    os::Window* window = nullptr;
    /// The settings the host's device was created with. Null with a host means "derive them from `create_info`"; without a host,
    /// the defaults.
    const RenderSettings* initial_settings = nullptr;
    /// The registry the renderer's transient pool registers with so it survives a device reset; null for none. The caller
    /// calls [[renderer::Renderer::Rebind]] with the new device and swapchain after the reset.
    DeviceResources* resources = nullptr;
    /// The frame callback.
    FrameBuildFn build = nullptr;
    /// Passed to the callback.
    void* build_user = nullptr;
};

/// What the renderer has done.
///
/// - Thread-safety: thread-compatible plain data.
struct RendererStats {
    /// Frames that ended `Ok`.
    u64 frames_rendered = 0;
    /// Frames that ended `Skipped`.
    u64 frames_skipped = 0;
    /// Frames that ended `Failed`.
    u64 frames_failed = 0;
    /// Frames that ended `DeviceLost`. The four frame counters add up to the frames begun.
    u64 frames_lost = 0;
    /// Times the swapchain reported `Recreated` (a resize); targets sized from it follow because the graph is rebuilt.
    u64 swapchain_recreations = 0;
    /// Settings applications that performed each cost class (`CostClass` as the index).
    u64 applies[5] = {};
    /// Device resets done by `ApplySettings`.
    u64 resets = 0;
    /// Resets or rebuilds that failed and were undone by restoring the previous settings.
    u64 rollbacks = 0;
    /// Device losses that were recovered from.
    u64 recoveries = 0;
    /// Recovery attempts that failed.
    u64 recovery_failures = 0;
    /// How long the last reset took, in milliseconds.
    f64 last_reset_ms = 0.0;
    /// The fence value of the latest submission.
    rhi::FenceValue last_fence = 0;
};

/// Builds the description a device is created with from settings: backend, adapter, validation, sync validation and frames in flight
/// come from `settings`, everything else from `base`.
///
/// - Param settings: the settings.
/// - Param base: the rest of the description (window, application name, faults).
/// - Param adapter_buffer: at least 64 bytes; holds the adapter text that `out->adapter` points to.
/// - Param out: receives the description.
/// - Ownership: `out->adapter` points into `adapter_buffer`, which must outlive the use of `out`.
void MakeDeviceCreateInfo(const RenderSettings& settings, const rhi::DeviceCreateInfo& base, char* adapter_buffer, rhi::DeviceCreateInfo* out);

/// Builds the swapchain description from settings: present mode, size, format and HDR.
///
/// - Param settings: the settings.
/// - Returns: the description.
rhi::SwapchainDesc MakeSwapchainDesc(const RenderSettings& settings);

/// What `Renderer::ApplySettings` did.
///
/// - Thread-safety: thread-compatible plain data (about 1.5 KB).
struct ApplyResult {
    /// The settings are in force (or were already).
    bool ok = false;
    /// The renderer was inside a frame; the request waits for the start of the next `RenderFrame` and nothing was done yet.
    bool queued = false;
    /// A step failed and the previous settings were restored; the renderer works as before.
    bool rolled_back = false;
    /// Restoring failed too: the renderer is in the device-lost state (`RenderFrame` returns `DeviceLost`).
    bool device_lost = false;
    /// The cost class actually performed.
    CostClass performed = CostClass::None;
    /// The settings that changed, and any that could not be applied for a missing capability.
    SettingsChange change;
    /// Why it failed; empty on success.
    char reason[192] = {};
    /// Milliseconds spent in the whole change.
    f64 total_ms = 0.0;
    /// Milliseconds spent releasing owners (a pipeline rebuild).
    f64 release_ms = 0.0;
    /// Milliseconds spent replacing the device (release, destroy, create, rebuild: the whole `DeviceHost::Recreate`).
    f64 device_ms = 0.0;
    /// Milliseconds spent reconfiguring the swapchain.
    f64 swapchain_ms = 0.0;
    /// Milliseconds spent rebuilding owners (a pipeline rebuild).
    f64 rebuild_ms = 0.0;
    /// For a device reset, where `device_ms` went: the host's split (owners releasing, destroying the old device, creating the
    /// device, the swapchain, owners rebuilding).
    rhi::ResetTimings reset;
};

/// Called when a settings change that touches the GPU starts. `stage` is "swapchain", "pipelines" or "device".
using ResetBeginFn = void (*)(void* user, const char* stage);
/// Called when it ends; `ok` is false if it failed (the renderer then restored the previous settings or is device-lost).
using ResetEndFn = void (*)(void* user, bool ok);

/// Renders frames.
///
/// - Thread-safety: owner-thread-only. Call from the thread that owns the swapchain and, when it has a scheduler, from its
///   participant 0.
/// - Ownership: borrows the device, swapchain and scheduler, all of which must outlive it; owns the transient pool (and so
///   the pooled textures, destroyed with it) and the frame arena.
class Renderer {
public:
    /// Creates the renderer.
    ///
    /// - Param desc: what to render with.
    /// - Aborts: the device or the callback is null.
    /// - Ownership: nothing in `desc` is taken over.
    explicit Renderer(const RendererDesc& desc);
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// Points the renderer at a new device and swapchain after the device was replaced. The renderer keeps no other device
    /// handle between frames: its transient pool is rebuilt through the registry, and the per-frame state is rebuilt by the
    /// next `RenderFrame`.
    ///
    /// - Param device: the new device.
    /// - Param swapchain: the new swapchain, or null for offscreen rendering.
    /// - Ownership: both are borrowed.
    /// - Aborts: `device` is null, or the renderer was built without `RendererDesc::resources` (its pool could not follow the device).
    void Rebind(rhi::Device* device, rhi::Swapchain* swapchain);

    /// Changes the rendering settings at run time, doing the cheapest sufficient thing.
    ///
    /// The change is diffed against the settings in force ([[renderer::DiffSettings]]) and its highest cost class is performed:
    /// `None` and `Dynamic` store the values; `Swapchain` reconfigures the swapchain (and asks the window for fullscreen);
    /// `PipelineRebuild` releases and recreates the registry's tier 3 and above (materials, passes); `DeviceReset` replaces the
    /// device through the host and every registered owner rebuilds. If a step fails the previous settings are restored; if that
    /// fails too the renderer is device-lost. Settings that need a missing capability (fullscreen, HDR) are left as they were.
    ///
    /// Called inside a frame (from the build callback, which runs on the owning thread) the request is queued and applied at the
    /// start of the next `RenderFrame`; a newer request replaces a pending one, also one made later outside a frame. Call from the
    /// owning thread only (never from a job); workers that record must be idle.
    ///
    /// - Param requested: the settings wanted.
    /// - Returns: what happened; `queued` if it waits for the frame boundary.
    /// - Fails: `ok` is false with `reason` set; `rolled_back` or `device_lost` say what state the renderer is in.
    ApplyResult ApplySettings(const RenderSettings& requested);

    /// Replaces the device with one made from the settings in force, although nothing changed (a reset on request).
    ///
    /// Same sequence, thread rule and failure handling as a device-reset change in `ApplySettings`.
    ///
    /// - Returns: what happened; `performed` is `DeviceReset` on success.
    /// - Fails: as for `ApplySettings`.
    ApplyResult ForceDeviceReset();

    /// Returns the settings in force.
    /// - Fails: never.
    const RenderSettings& Settings() const { return settings_; }

    /// Returns what the last `ApplySettings` (also one applied at a frame boundary) did.
    /// - Fails: never.
    const ApplyResult& LastApply() const { return last_apply_; }

    /// Returns what the device and window can do now, as the settings layer sees it.
    /// - Fails: never.
    const Capabilities& GetCapabilities() const { return caps_; }

    /// Sets the functions called around a settings change that touches the GPU, so the application can show a message, pause
    /// audio or drop its own GPU caches.
    ///
    /// - Param begin: called with the stage name; may be null.
    /// - Param end: called with the outcome; may be null.
    /// - Param user: passed to both.
    /// - Ownership: `user` is borrowed.
    void SetResetCallbacks(ResetBeginFn begin, ResetEndFn end, void* user) {
        reset_begin_ = begin;
        reset_end_ = end;
        reset_user_ = user;
    }

    /// Returns the device the renderer draws on now. After a reset or a rollback it is a new device: rebind your own pointers
    /// from this (also when the end callback reports failure, since a rollback creates the previous device again).
    /// - Fails: returns null once the renderer is broken (a failed change could not be undone).
    /// - Ownership: borrowed from the host.
    rhi::Device* GetDevice() const { return desc_.device; }

    /// Returns the swapchain the renderer presents to now, or null.
    /// - Fails: returns null without a swapchain or once the renderer is broken.
    /// - Ownership: borrowed from the host.
    rhi::Swapchain* GetSwapchain() const { return desc_.swapchain; }

    /// Returns whether the renderer is working to recover from a loss (frames return `Skipped` while it waits between attempts).
    /// - Fails: never.
    bool IsRecovering() const { return recovering_; }

    /// Returns whether a failed change left the renderer without a device (every frame returns `DeviceLost`).
    /// - Fails: never.
    bool IsBroken() const { return broken_; }

    /// Renders one frame.
    ///
    /// Opens a device frame, acquires the swapchain image (or imports `offscreen`), builds the graph through the
    /// callback, compiles and records it, submits once, closes the frame and presents. With a swapchain, every path that
    /// acquired an image also presents it, so a failed frame does not leave the swapchain stuck.
    ///
    /// - Param scene: passed to the callback; may be null.
    /// - Param camera: passed to the callback; may be null.
    /// - Param offscreen: the target when there is no swapchain; ignored with one.
    /// - Returns: how the frame ended.
    /// - Fails: `Skipped` when the swapchain is minimized; `DeviceLost` when the device or surface is lost; `Failed` for
    ///   anything else (the reason is in `LastError`).
    /// - Aborts: there is no swapchain and `offscreen` is null or has an invalid texture.
    FrameStatus RenderFrame(const RenderScene* scene, const Camera* camera, const FrameTarget* offscreen);

    /// Returns why the last frame failed (empty if it did not).
    /// - Fails: never.
    /// - Ownership: borrowed; valid until the next frame.
    const char* LastError() const { return error_; }

    /// Returns the counters.
    /// - Fails: never.
    RendererStats Stats() const { return stats_; }

    /// Returns the transient pool, for its counters.
    /// - Fails: never.
    const TransientPool& Pool() const { return pool_; }

    /// Returns the number of frames begun (the next frame's index minus 1).
    /// - Fails: never.
    u64 FrameIndex() const { return frame_; }

private:
    void SetError(const char* fmt, ...) ENGINE_PRINTF_FORMAT(2, 3);
    FrameStatus RenderFrameImpl(const RenderScene* scene, const Camera* camera, const FrameTarget* offscreen);
    ApplyResult ApplyNow(const RenderSettings& requested, bool force_device_reset = false);
    void BuildCreateInfo(const RenderSettings& settings, char* adapter_buffer, rhi::DeviceCreateInfo* out) const {
        MakeDeviceCreateInfo(settings, desc_.create_info, adapter_buffer, out);
    }
    static rhi::SwapchainDesc BuildSwapchainDesc(const RenderSettings& settings) { return MakeSwapchainDesc(settings); }
    void RefreshCapabilities(bool adapters_too);
    bool RollbackDevice(ApplyResult* result);
    bool RecoveryAttempt();
    void NoteLost(); // starts recovery after a frame ended DeviceLost
    FrameStatus Abandon(FrameStatus status, bool acquired);

    RendererDesc desc_;
    RenderSettings settings_;
    RenderSettings pending_;
    bool has_pending_ = false;
    bool in_frame_ = false;
    bool broken_ = false;
    bool recovering_ = false;
    bool force_full_recovery_ = false;
    u32 recovery_attempt_ = 0;
    u32 recovery_wait_ = 0;
    Capabilities caps_;
    ApplyResult last_apply_;
    rhi::DeviceCreateInfo active_info_;
    char active_adapter_[64] = {};
    ResetBeginFn reset_begin_ = nullptr;
    ResetEndFn reset_end_ = nullptr;
    void* reset_user_ = nullptr;
    ArenaAllocator arena_;
    TransientPool pool_;
    RendererStats stats_;
    u64 frame_ = 0;
    char error_[kGraphErrorLength] = {};
};

} // namespace engine_placeholder::renderer
