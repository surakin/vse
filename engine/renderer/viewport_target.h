#pragma once

#include "renderer/device_resources.h"
#include "renderer/render_graph.h"
#include "renderer/renderer.h"
#include "renderer/ui_renderer.h"
#include "rhi/rhi.h"
#include "types.h"

//! The offscreen target of a UI viewport (Phase 15, Day 201): the colour and depth textures the world is drawn into,
//! shown by the UI pass as an image.

namespace engine_placeholder::renderer
{

/// What a [[renderer::ViewportTarget]] is made of.
///
/// - Thread-safety: thread-compatible plain data.
struct ViewportTargetDesc
{
    /// The colour format; the world's last pass (the tonemap) writes it and the UI pass samples it. Must be usable as a
    /// colour attachment and as a sampled texture.
    rhi::Format color_format = rhi::Format::RGBA8_UNorm;
    /// Whether the target also owns a depth texture of `depth_format` (the world needs one when a pass depth-tests
    /// straight into it; the deferred path has its own).
    bool with_depth = false;
    /// The depth format when `with_depth` is set.
    rhi::Format depth_format = rhi::Format::D32_Float;
};

/// The render target of one viewport: a colour texture (and optionally a depth texture) of the viewport's size in
/// physical pixels.
///
/// A *render target* is a role a texture plays, not a kind of resource: this object owns the textures, and a frame uses
/// the colour one as a `ColorAttachment` while the world is drawn and as a `Sampled` texture while the UI pass reads
/// it. [[renderer::ViewportTarget::ImportColor]] brings the texture into a frame's graph so the world's passes write it
/// and
/// [[renderer::UiRenderer::AddToGraph]] (given the same resource in `sampled`) reads it, in that order.
///
/// `Resize` makes the textures of the new size first and destroys the old ones after; the device keeps a destroyed
/// texture alive until the GPU has finished with it, so a frame in flight that still reads the old picture is safe. It
/// is also an [[renderer::ImageSource]]: register it with `UiRenderer::SetImage` and the UI draws it.
///
/// It registers with a [[renderer::DeviceResources]] at the textures tier. After a reset or a loss `Recreate` makes
/// textures of the same size again; their contents are lost until the next frame draws the world (the UI shows nothing
/// for an image whose texture is missing, and the picture returns one frame after the reset).
///
/// - Thread-safety: owner-thread-only.
/// - Ownership: owns its textures; the device and the registry must outlive it.
class ViewportTarget final : public Recreatable, public ImageSource
{
public:
    ViewportTarget() = default;
    /// Releases everything this object owns.
    ~ViewportTarget() override { Destroy(); }
    ViewportTarget(const ViewportTarget&) = delete;
    ViewportTarget& operator=(const ViewportTarget&) = delete;

    /// Creates the textures.
    ///
    /// - Param device: the device.
    /// - Param desc: the formats.
    /// - Param width: width in pixels; at least 1.
    /// - Param height: height in pixels; at least 1.
    /// - Param resources: the registry, or null for a target that does not need to survive a reset.
    /// - Param debug_name: a name for debuggers; copied.
    /// - Returns: true on success.
    /// - Fails: returns false, logging the reason, if a size is 0 or above the device limit, the format cannot be used
    /// so, or memory runs out; nothing stays allocated.
    /// - Aborts: the object already exists or `device` is null.
    /// - Ownership: the device and the registry are borrowed.
    bool Create(rhi::Device* device, const ViewportTargetDesc& desc, u32 width, u32 height, DeviceResources* resources,
                const char* debug_name);

    /// Destroys the textures and forgets the registration.
    void Destroy();

    /// Gives the target a new size: makes the new textures, then destroys the old ones. The same size again does
    /// nothing and returns true. Between a `Release` and the `Recreate` after it only the size is recorded, and the
    /// recreate uses it.
    ///
    /// - Returns: true when the target has the requested size afterwards.
    /// - Fails: returns false, keeping the old textures and size, when a size is 0, above the device limit, or the new
    /// textures cannot be made.
    /// - Aborts: the object does not exist.
    bool Resize(u32 width, u32 height);

    /// Returns the colour texture, or an invalid handle after a reset released it. The [[renderer::ImageSource]]
    /// answer.
    ///
    /// - Fails: never.
    rhi::TextureHandle Handle() const override { return color_; }
    /// Returns the depth texture, or an invalid handle when the target has none or after a reset released it.
    ///
    /// - Fails: never.
    rhi::TextureHandle DepthHandle() const { return depth_; }
    /// Returns the width in pixels.
    ///
    /// - Fails: never.
    u32 Width() const { return width_; }
    /// Returns the height in pixels.
    ///
    /// - Fails: never.
    u32 Height() const { return height_; }
    /// Returns the colour format.
    ///
    /// - Fails: never.
    rhi::Format ColorFormat() const { return desc_.color_format; }
    /// Returns how many times `Resize` changed the size.
    ///
    /// - Fails: never.
    u64 ResizeCount() const { return resizes_; }

    /// Brings the colour texture into `graph` for this frame. The graph leaves it in the sampled state, which is also
    /// where the next frame finds it. The target records that new state at import, so the graph must be executed: a
    /// frame abandoned after the import (the build callback returns false) leaves the record wrong; call `Resize` to
    /// the same size plus one pixel and back, or recreate the target, to reset it.
    ///
    /// - Returns: the graph resource the world's last pass writes (as `ColorAttachment`) and the UI pass reads (in
    /// `sampled`).
    /// - Aborts: `graph` is null or the object holds no texture.
    /// - Ownership: borrows `graph`; the resource is valid until the graph is executed.
    GraphResource ImportColor(RenderGraph* graph);
    /// Brings the depth texture into `graph` for this frame; it is left in its depth-attachment state.
    ///
    /// - Aborts: `graph` is null, or the target has no depth texture.
    /// - Ownership: borrows `graph`; the resource is valid until the graph is executed.
    GraphResource ImportDepth(RenderGraph* graph);

    /// Describes the colour texture as the target of `Renderer::RenderFrame` when the world is drawn in a frame of its
    /// own (its own submission): the frame leaves it ready to sample.
    ///
    /// - Aborts: the object holds no texture.
    FrameTarget AsFrameTarget();

    void Release() override;
    bool Recreate(rhi::Device* device) override;
    bool Live() const override { return color_.IsValid(); }
    const char* RecreatableName() const override { return name_; }

private:
    bool Make(rhi::Device* device, u32 width, u32 height, rhi::TextureHandle* color, rhi::TextureHandle* depth);

    rhi::Device* device_ = nullptr;
    ViewportTargetDesc desc_;
    u32 width_ = 0;
    u32 height_ = 0;
    rhi::TextureHandle color_;
    rhi::TextureHandle depth_;
    rhi::ResourceState color_state_ = rhi::ResourceState::Undefined;
    rhi::ResourceState depth_state_ = rhi::ResourceState::Undefined;
    u64 resizes_ = 0;
    char name_[48] = "viewport target";
    ScopedRegistration registration_;
};

}  // namespace engine_placeholder::renderer
