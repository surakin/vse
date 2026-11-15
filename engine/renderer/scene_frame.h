#pragma once

//! The frame of a lit scene: the material system, the per-frame buffers, the G-buffer, lighting and tonemap passes, an
//! optional text overlay, and the [[Renderer]] frame loop that runs them. One object per device. Moved here from
//! the renderer demo on Phase 24 Day 359 so the C API's surfaces draw worlds with it; the demo and the tests use it
//! too.

#include "containers/unique_ptr.h"
#include "renderer/frame_data.h"
#include "renderer/gbuffer.h"
#include "renderer/lighting.h"
#include "renderer/material.h"
#include "renderer/renderer.h"
#include "renderer/text_renderer.h"
#include "containers/vector.h"
#include "string_view.h"
#include "text/font.h"
#include "text/glyph_atlas.h"
#include "rhi/rhi.h"
#include "types.h"

namespace engine_placeholder::jobs
{
class Scheduler;
}

namespace engine_placeholder::renderer
{

/// What a [[renderer::SceneFrame]] is built from.
///
/// - Thread-safety: thread-compatible plain data.
struct SceneFrameSetup
{
    /// The device the frame draws on (required).
    rhi::Device* device = nullptr;
    /// The swapchain to present to, or null to render to a FrameTarget given to each Render call.
    rhi::Swapchain* swapchain = nullptr;
    /// The most objects and lights one frame writes.
    u32 max_objects = 1024;
    /// The most lights one frame writes.
    u32 max_lights = 1024;
    /// A G-buffer layer to show instead of the lit picture, or -1 for the lit picture.
    int gbuffer_layer = -1;
    /// The tonemap exposure.
    f32 exposure = 1.0f;
    /// The job scheduler the frame's passes record on, or null to record on the calling thread.
    jobs::Scheduler* scheduler = nullptr;
    /// The registry every device-owning part registers with so the frame survives a device reset, or null.
    DeviceResources* resources = nullptr;
    /// Rebuild pipelines in the reset (true) or when they are next used (false).
    bool prewarm_pipelines = false;
    /// The device host the renderer replaces the device through (for Renderer::ApplySettings and recovery), or null.
    rhi::DeviceHost* host = nullptr;
    /// What a replacement device is created with besides the settings.
    rhi::DeviceCreateInfo create_info;
    /// The settings the host's device was made from, or null.
    const RenderSettings* initial_settings = nullptr;
    /// The window the swapchain shows, for fullscreen, or null.
    os::Window* window = nullptr;
    /// What to do when the device is lost.
    RecoveryPolicy recovery;
    /// Called with the swapchain image of a frame that presents, before the present (BeforePresentFn), or null.
    BeforePresentFn before_present = nullptr;
    /// Passed to `before_present`.
    void* before_present_user = nullptr;
    /// The font file the text overlay draws with, or null for a frame without the overlay (`SetOverlay` then does
    /// nothing).
    const char* font_path = nullptr;
};

/// Lets a program put the 3D scene in a texture of its own and draw more over the frame's target (the UI demo: the
/// scene goes to a viewport's texture, then the UI pass draws the window).
///
/// - Thread-safety: thread-compatible plain data.
struct SceneFrameCompose
{
    /// Called while the graph is built, before the scene's passes: returns the target, size and format the scene is
    /// drawn to (false abandons the frame).
    bool (*scene_target)(const FrameContext& context, void* user, GraphResource* target, u32* width, u32* height,
                         rhi::Format* format) = nullptr;
    /// Called after all of the frame's own passes: declares more passes over `context.target`.
    void (*after_scene)(const FrameContext& context, void* user) = nullptr;
    /// Passed to both callbacks.
    void* user = nullptr;
};

/// The frame of a lit scene: owns the material system, the per-frame buffers and the passes, and renders
/// [[renderer::RenderScene]]s with them.
///
/// - Thread-safety: owner-thread-only: every call on the thread that renders.
/// - Ownership: owns its parts; borrows the device, swapchain, scheduler and registry of the setup, which must outlive
/// it.
class SceneFrame
{
public:
    /// An empty frame; call [[renderer::SceneFrame::Create]].
    SceneFrame();
    /// Calls [[renderer::SceneFrame::Destroy]].
    ~SceneFrame();
    SceneFrame(const SceneFrame&) = delete;
    SceneFrame& operator=(const SceneFrame&) = delete;

    /// Creates everything. Returns false, logging why, if a part could not be created (the G-buffer formats are
    /// unsupported, a shader or a pipeline fails).
    ///
    /// - Fails: returns false, with the reason logged.
    bool Create(const SceneFrameSetup& setup);
    /// Points the frame at the device and swapchain a reset made; the parts rebuilt themselves through the registry.
    ///
    /// - Ownership: both are borrowed.
    void Rebind(rhi::Device* device, rhi::Swapchain* swapchain);
    /// Releases everything; idle the device first. The destructor calls it.
    void Destroy();

    /// The renderer, for ApplySettings and its statistics; null before Create.
    ///
    /// - Fails: never.
    /// - Ownership: owned by the frame.
    Renderer* GetRenderer() { return renderer_.Get(); }
    /// The material system; null before Create.
    ///
    /// - Fails: never.
    /// - Ownership: owned by the frame.
    MaterialSystem* Materials() { return materials_.Get(); }
    /// The G-buffer pipeline templates materials are made from.
    const GBufferTemplates& Templates() const { return templates_; }

    /// Sets the compose hooks (see FrameCompose); call before the first Render.
    void SetCompose(const SceneFrameCompose& compose) { compose_ = compose; }
    /// Sets the clock the next frame's FrameData carries.
    void SetTime(f32 seconds, f32 delta_seconds)
    {
        time_ = seconds;
        delta_ = delta_seconds;
    }
    /// Renders one frame of `scene` seen by `camera`. See Renderer::RenderFrame.
    FrameStatus Render(const RenderScene* scene, const Camera* camera, const FrameTarget* offscreen);

    /// The text overlay (Phase 14): `greeting` at 32 px and `status` at 14 px below it, drawn after the tonemap while
    /// `on` is true. Needs `FrameSetup::font_path`. Returns false when the frame has no overlay (no font was given or
    /// it could not be opened).
    ///
    /// - Fails: returns false when the frame has no overlay.
    bool SetOverlay(bool on, StringView greeting, StringView status);
    /// True when the overlay's font opened.
    ///
    /// - Fails: never.
    bool OverlayAvailable() const { return font_.IsOpen(); }
    /// True while the overlay is drawn.
    ///
    /// - Fails: never.
    bool OverlayOn() const { return overlay_on_; }
    /// The overlay's display scale: its sizes and margins are multiplied by it (a window's `ContentScale`, 2 on a 200%
    /// display; 1 for screenshots).
    void SetOverlayScale(f32 scale) { overlay_scale_ = scale > 0.0f ? scale : 1.0f; }
    /// What the overlay drew: its renderer's counters.
    TextStats OverlayStats() const { return text_.Stats(); }

    /// The renderer's last error text, or "not created".
    ///
    /// - Fails: never.
    /// - Ownership: owned by the renderer; valid until the next frame.
    const char* LastError() const { return renderer_ ? renderer_->LastError() : "not created"; }
    /// The renderer's statistics.
    RendererStats Stats() const { return renderer_ ? renderer_->Stats() : RendererStats{}; }
    /// The transient pool's counters.
    TransientPoolStats PoolStats() const { return renderer_ ? renderer_->Pool().Stats() : TransientPoolStats{}; }
    /// What the last frame drew.
    const GBufferStats& GBufferStatistics() const { return gbuffer_stats_; }
    /// The lights of the last frame.
    const LightSummary& Lights() const { return light_summary_; }
    /// The point-light draws the last frame recorded.
    u32 PointDraws() const { return lighting_ ? lighting_->PointDrawsRecorded() : 0; }
    /// Rejected objects of the last frame (see GBufferPass::Prepare).
    ///
    /// - Fails: never.
    bool LastGBufferOk() const { return gbuffer_ok_; }

private:
    static bool Build(const FrameContext& context, void* user);

    rhi::Device* device_ = nullptr;
    UniquePtr<MaterialSystem> materials_;
    UniquePtr<FrameResources> frame_;
    UniquePtr<GBufferPass> gbuffer_;
    UniquePtr<LightingPass> lighting_;
    UniquePtr<TonemapPass> tonemap_;
    UniquePtr<GBufferDebugView> debug_view_;
    UniquePtr<Renderer> renderer_;
    GBufferTemplates templates_;
    GBufferStats gbuffer_stats_;
    LightSummary light_summary_;
    text::FontLibrary font_library_;
    text::Font font_;
    text::GlyphAtlas atlas_;
    TextRenderer text_;
    bool overlay_on_ = false;
    f32 overlay_scale_ = 1.0f;
    Vector<char> greeting_;
    Vector<char> status_;
    bool gbuffer_ok_ = true;
    int gbuffer_layer_ = -1;
    f32 exposure_ = 1.0f;
    SceneFrameCompose compose_;
    f32 time_ = 0.0f;
    f32 delta_ = 0.0f;
};

}  // namespace engine_placeholder::renderer
