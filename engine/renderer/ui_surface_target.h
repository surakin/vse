#pragma once

#include "renderer/render_graph.h"
#include "renderer/ui_renderer.h"
#include "renderer/viewport_target.h"
#include "rhi/rhi.h"
#include "types.h"
#include "ui/draw_list.h"

//! The texture a UI surface is drawn into so the 3D world can show it (Phase 15, Day 202): a screen on a wall, a monitor, a floating panel.

namespace engine_placeholder::renderer {

/// What a [[renderer::UiSurfaceTarget]] is made of.
///
/// Thread-safety: thread-compatible plain data.
struct UiSurfaceTargetDesc {
    /// The colour format. An sRGB format makes the UI show the colours the theme says when a material samples it as sRGB (the sampler decodes, the UI pass encodes); a UNORM format
    /// stores the colours as they are, for a material that treats them as already linear. It must match the format of the [[renderer::UiRenderer]] that draws into it.
    rhi::Format format = rhi::Format::RGBA8_SRGB;
    /// The colour the target is cleared to before the UI is drawn, packed `0xAABBGGRR`. Alpha 0 gives a transparent surface (an overlay whose material blends it over the world).
    u32 background = 0xFF000000u;
};

/// The offscreen colour texture of one UI surface, drawn by one [[renderer::UiRenderer]] and sampled by the world's passes.
///
/// It is an [[renderer::ImageSource]] and a [[renderer::Recreatable]] (through its [[renderer::ViewportTarget]]), so a material or the UI can sample it and a device reset
/// rebuilds it. After a reset or a resize the texture is empty: `NeedsPaint` is true until the next `AddToGraph`, and the application draws the surface again whether or not it
/// changed. Between paints the texture keeps its last picture, so an idle surface adds no pass and no GPU work.
///
/// Per frame: `BeginFrame`, then `AddToGraph` when the surface was rebuilt (or `NeedsPaint`), then `Sampled` wherever a world pass reads it; the returned resource is the same one
/// in both cases, and the graph orders the readers after the paint. Use one `UiRenderer` per surface (its pass is one per frame per renderer).
///
/// Thread-safety: owner-thread-only.
/// Ownership: owns its textures; the device and the registry must outlive it.
class UiSurfaceTarget final : public ImageSource {
public:
    UiSurfaceTarget() = default;

    /// Creates the texture.
    ///
    /// Param device: the device.
    /// Param desc: the format and background.
    /// Param width: width in pixels; at least 1.
    /// Param height: height in pixels; at least 1.
    /// Param resources: the registry, or null for a target that does not need to survive a reset.
    /// Param debug_name: a name for debuggers; copied.
    /// Returns: true on success.
    /// Fails: as [[renderer::ViewportTarget::Create]].
    /// Aborts: as [[renderer::ViewportTarget::Create]].
    /// Ownership: the device and the registry are borrowed.
    bool Create(rhi::Device* device, const UiSurfaceTargetDesc& desc, u32 width, u32 height, DeviceResources* resources, const char* debug_name);
    /// Destroys the texture.
    void Destroy() { target_.Destroy(); }

    /// Gives the target a new size, as [[renderer::ViewportTarget::Resize]]; the picture is lost, so `NeedsPaint` becomes true.
    ///
    /// Returns: true when the target has the requested size afterwards.
    /// Fails: as [[renderer::ViewportTarget::Resize]].
    bool Resize(u32 width, u32 height);

    /// Changes the background colour; the next paint uses it (`NeedsPaint` becomes true).
    void SetBackground(u32 abgr);

    /// Returns the current texture, or an invalid handle after a reset released it.
    ///
    /// Fails: never.
    rhi::TextureHandle Handle() const override { return target_.Handle(); }
    /// Returns the width in pixels.
    ///
    /// Fails: never.
    u32 Width() const { return target_.Width(); }
    /// Returns the height in pixels.
    ///
    /// Fails: never.
    u32 Height() const { return target_.Height(); }
    /// Returns the colour format.
    ///
    /// Fails: never.
    rhi::Format Format() const { return target_.ColorFormat(); }
    /// Returns whether the texture holds no picture of the surface yet (new, resized, recreated after a reset, or the background changed): paint it with `AddToGraph`.
    ///
    /// Fails: never.
    bool NeedsPaint() const { return !painted_ || target_.Handle() != painted_handle_; }
    /// Returns how many times the surface was painted.
    ///
    /// Fails: never.
    u64 Paints() const { return paints_; }

    /// Starts a frame: forgets the graph resource of the previous one. Call it once per frame before `AddToGraph` and `Sampled`.
    void BeginFrame() { frame_resource_ = GraphResource(); has_resource_ = false; }

    /// Paints the surface: imports the texture, clears it to the background and draws `list` over it with `ui_renderer`.
    ///
    /// Param graph: the frame's graph.
    /// Param ui_renderer: the renderer that draws this surface; its colour format must be this target's.
    /// Param list: the surface's sorted draw list; borrowed until the graph is executed. An empty list gives a cleared texture.
    /// Returns: the graph resource of the texture, written by the paint.
    /// Aborts: `graph` or `ui_renderer` is null, the target holds no texture, or the target is already in this frame's graph (paint before `Sampled`, once).
    /// Ownership: borrows `graph`, `ui_renderer` and `list`; the resource is valid until the graph is executed.
    GraphResource AddToGraph(RenderGraph* graph, UiRenderer* ui_renderer, const ui::DrawList* list);

    /// Returns the graph resource of the texture for a pass that reads it this frame: the one `AddToGraph` made, or an import of the texture as it was left (no paint this frame).
    /// A pass that samples it declares `Read(resource, GraphUsage::Sampled)`.
    ///
    /// Aborts: `graph` is null or the target holds no texture.
    /// Ownership: borrows `graph`; the resource is valid until the graph is executed.
    GraphResource Sampled(RenderGraph* graph);

    /// Returns the viewport target that owns the texture (for tests that count its resizes).
    ///
    /// Fails: never.
    const ViewportTarget& Target() const { return target_; }

private:
    static void ClearPass(const PassExecuteContext& context, void* user);

    ViewportTarget target_;
    u32 background_ = 0xFF000000u;
    bool painted_ = false;
    rhi::TextureHandle painted_handle_;
    u64 paints_ = 0;
    GraphResource frame_resource_;
    bool has_resource_ = false;
    GraphResource clear_target_;
    f32 clear_color_[4] = {0, 0, 0, 1};
};

} // namespace engine_placeholder::renderer
