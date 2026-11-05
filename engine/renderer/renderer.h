#pragma once

//! The renderer's frame loop: begins a frame, builds and executes the render graph, and presents.
//!
//! A [[renderer::Renderer]] owns what lives across frames (the transient pool, the per-frame arena) and borrows the
//! device, the optional swapchain and the optional job scheduler. A frame is built by a callback that declares the passes
//! on a fresh [[renderer::RenderGraph]]; the renderer then compiles it, records the passes (in parallel when it has a
//! scheduler), submits once and presents. Later days supply the real callback (G-buffer, lighting, tonemap); tests supply
//! their own.

#include "mem/arena_allocator.h"
#include "renderer/camera.h"
#include "renderer/render_graph.h"
#include "renderer/render_scene.h"
#include "renderer/transient_pool.h"
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
/// Param status: any status.
/// Returns: a static string.
/// Fails: never.
/// Ownership: the string is static and never freed.
const char* FrameStatusName(FrameStatus status);

/// The texture a frame renders to when there is no swapchain.
///
/// Thread-safety: thread-compatible plain data.
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
/// Thread-safety: thread-compatible plain data; valid only during the callback.
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

/// What a [[renderer::Renderer]] is made of.
///
/// Thread-safety: thread-compatible plain data.
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
    /// The frame callback.
    FrameBuildFn build = nullptr;
    /// Passed to the callback.
    void* build_user = nullptr;
};

/// What the renderer has done.
///
/// Thread-safety: thread-compatible plain data.
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
    /// The fence value of the latest submission.
    rhi::FenceValue last_fence = 0;
};

/// Renders frames.
///
/// Thread-safety: owner-thread-only. Call from the thread that owns the swapchain and, when it has a scheduler, from its
///   participant 0.
/// Ownership: borrows the device, swapchain and scheduler, all of which must outlive it; owns the transient pool (and so
///   the pooled textures, destroyed with it) and the frame arena.
class Renderer {
public:
    /// Creates the renderer.
    ///
    /// Param desc: what to render with.
    /// Aborts: the device or the callback is null.
    /// Ownership: nothing in `desc` is taken over.
    explicit Renderer(const RendererDesc& desc);
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// Renders one frame.
    ///
    /// Opens a device frame, acquires the swapchain image (or imports `offscreen`), builds the graph through the
    /// callback, compiles and records it, submits once, closes the frame and presents. With a swapchain, every path that
    /// acquired an image also presents it, so a failed frame does not leave the swapchain stuck.
    ///
    /// Param scene: passed to the callback; may be null.
    /// Param camera: passed to the callback; may be null.
    /// Param offscreen: the target when there is no swapchain; ignored with one.
    /// Returns: how the frame ended.
    /// Fails: `Skipped` when the swapchain is minimized; `DeviceLost` when the device or surface is lost; `Failed` for
    ///   anything else (the reason is in `LastError`).
    /// Aborts: there is no swapchain and `offscreen` is null or has an invalid texture.
    FrameStatus RenderFrame(const RenderScene* scene, const Camera* camera, const FrameTarget* offscreen);

    /// Returns why the last frame failed (empty if it did not).
    /// Fails: never.
    /// Ownership: borrowed; valid until the next frame.
    const char* LastError() const { return error_; }

    /// Returns the counters.
    /// Fails: never.
    RendererStats Stats() const { return stats_; }

    /// Returns the transient pool, for its counters.
    /// Fails: never.
    const TransientPool& Pool() const { return pool_; }

    /// Returns the number of frames begun (the next frame's index minus 1).
    /// Fails: never.
    u64 FrameIndex() const { return frame_; }

private:
    void SetError(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    FrameStatus Abandon(FrameStatus status, bool acquired);

    RendererDesc desc_;
    ArenaAllocator arena_;
    TransientPool pool_;
    RendererStats stats_;
    u64 frame_ = 0;
    char error_[kGraphErrorLength] = {};
};

} // namespace engine_placeholder::renderer
