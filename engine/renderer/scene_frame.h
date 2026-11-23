#pragma once

//! The frame of a lit scene: the material system, the per-frame buffers, the G-buffer, lighting and tonemap passes, an
//! optional text overlay, and the [[Renderer]] frame loop that runs them. One object per device. The C API's surfaces
//! draw worlds with it; the renderer demo and the tests use it too.

#include "containers/hash_map.h"
#include "containers/unique_ptr.h"
#include "renderer/frame_data.h"
#include "renderer/gbuffer.h"
#include "renderer/lighting.h"
#include "renderer/sky.h"
#include "renderer/material.h"
#include "renderer/renderer.h"
#include "renderer/text_renderer.h"
#include "containers/vector.h"
#include "string/string.h"
#include "string_view.h"
#include "text/font.h"
#include "text/glyph_atlas.h"
#include "rhi/rhi.h"
#include "types.h"

namespace vse::jobs
{
class Scheduler;
}

namespace vse::renderer
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
    /// The most joint matrices one frame writes (skinned meshes' palettes), or 0: then `renderer.joint_palette_bytes`
    /// of `initial_settings` divided by 48, or 1024 without settings.
    u32 max_joint_matrices = 0;
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

namespace detail
{

// The key of a cached label layout: the pixel size and the text (container audit Day 7: it was a bare 64-bit hash
// with a collision fallback). A `LabelKeyView` borrows the text, so a lookup makes no string.
struct LabelKey
{
    u32 pixel_size = 0;
    String<HeapAllocator> text{HeapAllocator::Default()};
};
struct LabelKeyView
{
    u32 pixel_size = 0;
    StringView text;
};
inline bool operator==(const LabelKey& a, const LabelKey& b)
{
    return a.pixel_size == b.pixel_size && a.text.View() == b.text.View();
}
inline bool operator==(const LabelKey& a, const LabelKeyView& b)
{
    return a.pixel_size == b.pixel_size && a.text.View() == b.text;
}
inline usize HashLabel(u32 pixel_size, StringView text)
{
    return HashCombine(Hash<u32>{}(pixel_size), Hash<StringView>{}(text));
}

}  // namespace detail

}  // namespace vse::renderer

namespace vse
{

/// Hash function for the scene frame's label keys.
///
/// - Thread-safety: thread-safe (stateless).
template <> struct Hash<renderer::detail::LabelKey>
{
    /// Returns the hash of the pixel size and the text, equal to the view's.
    usize operator()(const renderer::detail::LabelKey& k) const
    {
        return renderer::detail::HashLabel(k.pixel_size, k.text.View());
    }
};
/// Hash function for a borrowed label key, equal to the owned key's for equal contents.
///
/// - Thread-safety: thread-safe (stateless).
template <> struct Hash<renderer::detail::LabelKeyView>
{
    /// Returns the hash of the pixel size and the text.
    usize operator()(const renderer::detail::LabelKeyView& k) const
    {
        return renderer::detail::HashLabel(k.pixel_size, k.text);
    }
};

}  // namespace vse

namespace vse::renderer
{

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
    /// Sets the exposure of the tone map and the linear colour behind the scene for the next frames.
    ///
    /// - Param exposure: positive.
    /// - Param background: linear RGB, not negative.
    /// - Param sky_reflection: how much of the background the lit surfaces reflect, not negative; 0 reflects nothing.
    /// - Fails: never.
    void SetEnvironment(f32 exposure, const Vec3& background, f32 sky_reflection = 0.0f)
    {
        exposure_ = exposure;
        SkySettings sky;
        sky.mode = SkyMode::Color;
        sky.color = background;
        SetSky(sky, sky_reflection);
    }
    /// Sets the sky for the next frames: a flat colour (the lit surfaces reflect it as `SetEnvironment` says), a
    /// procedural gradient with a sun, or nothing. A per-frame value: no pipeline or resource is rebuilt.
    ///
    /// Mode `Color` clears the accumulation target to `settings.color`; `Procedural` clears it to black and draws the
    /// sky where the depth buffer holds the far plane; `None` clears to black. A procedural sky's reflection colour is
    /// its horizon colour times its exposure.
    ///
    /// - Param settings: valid settings ([[renderer::SkySettingsValid]]).
    /// - Param sky_reflection: how much of the sky the lit surfaces reflect, not negative.
    /// - Aborts: the settings are invalid.
    void SetSky(const SkySettings& settings, f32 sky_reflection = 0.0f);
    /// Returns the sky last set.
    /// - Fails: never.
    const SkySettings& Sky() const { return sky_settings_; }
    /// Renders one frame of `scene` seen by `camera`. See Renderer::RenderFrame.
    FrameStatus Render(const RenderScene* scene, const Camera* camera, const FrameTarget* offscreen);

    /// The text overlay: `greeting` at 32 px and `status` at 14 px below it, drawn after the tonemap while
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

    /// Opens the font the labels draw with from the bytes of a font file (copied) and makes the text renderer, when
    /// the frame has none yet (a frame made with `font_path` already has).
    ///
    /// - Returns: true when labels can be drawn.
    /// - Fails: returns false (logging) for bytes that are no font or a device without the text shaders.
    bool OpenLabelFont(ByteSpan data);
    /// True when labels can be drawn.
    ///
    /// - Fails: never.
    bool LabelsAvailable() const { return font_.IsOpen() && text_.Live(); }
    /// Forgets the labels of the previous frame; call it before `AddLabel`.
    ///
    /// - Fails: never.
    void BeginLabels();
    /// Adds a label to the frame: `utf8` at `pixel_size` in `color` (packed RGBA, red lowest), its anchor point at
    /// pixel (`x`, `y`) of the target; `anchor` 0 centres the text horizontally on x, 1 starts it there, 2 ends it
    /// there; the text's vertical middle is on y. Drawn after the tonemap, over the picture.
    ///
    /// - Returns: true when the label was queued.
    /// - Fails: returns false when no font is open or 1024 labels are queued already.
    bool AddLabel(StringView utf8, f32 x, f32 y, u32 pixel_size, u32 color, u32 anchor);
    /// Labels waiting for the next frame.
    ///
    /// - Fails: never.
    u32 LabelsQueued() const { return static_cast<u32>(labels_.Size()); }
    /// Glyphs that were the font's `.notdef` in the labels drawn so far.
    ///
    /// - Fails: never.
    u64 LabelMissingGlyphs() const { return label_missing_; }
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
    const text::TextLayout* LabelLayout(StringView text, const text::TextStyle& style, u64 frame);
    void DropUnusedLabels(u64 frame);
    static bool Build(const FrameContext& context, void* user);

    rhi::Device* device_ = nullptr;
    UniquePtr<MaterialSystem> materials_;
    UniquePtr<FrameResources> frame_;
    UniquePtr<GBufferPass> gbuffer_;
    UniquePtr<LightingPass> lighting_;
    UniquePtr<TonemapPass> tonemap_;
    UniquePtr<SkyPass> sky_;
    GpuTexturePtr environment_cube_;  // the procedural sky's cube, for the lighting pass
    SkySettings environment_source_;  // the settings it was made from
    SkySettings sky_settings_;
    f32 sky_reflection_ = 0.0f;
    UniquePtr<GBufferDebugView> debug_view_;
    UniquePtr<Renderer> renderer_;
    GBufferTemplates templates_;
    GBufferStats gbuffer_stats_;
    LightSummary light_summary_;
    text::FontLibrary font_library_;
    text::Font font_;
    text::GlyphAtlas atlas_;
    TextRenderer text_;
    struct Label
    {
        f32 x = 0;
        f32 y = 0;
        u32 pixel_size = 16;
        u32 color = 0xFFFFFFFFu;
        u32 anchor = 0;
        u32 text_begin = 0;
        u32 text_length = 0;
    };
    Vector<Label> labels_;
    Vector<char> label_text_;
    // The layouts of the labels drawn last frame: a host adds its labels again every frame, and most are unchanged
    // (container audit Days 6 and 7: every label was laid out every frame). Entries a frame does not use are dropped
    // after it, so the cache holds at most one frame's labels.
    struct CachedLabel
    {
        text::TextLayout layout;
        u32 missing = 0;  // glyphs the font lacks
        u64 used_frame = 0;
    };
    HashMap<detail::LabelKey, CachedLabel, HeapAllocator> label_cache_{HeapAllocator::Default()};
    u64 label_frame_ = 0;
    u64 label_missing_ = 0;
    DeviceResources* resources_ = nullptr;
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

}  // namespace vse::renderer
