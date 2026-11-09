#pragma once

#include "containers/vector.h"
#include "renderer/device_resources.h"
#include "renderer/glyph_atlas_texture.h"
#include "renderer/render_graph.h"
#include "renderer/texture.h"
#include "rhi/rhi.h"
#include "types.h"
#include "ui/canvas.h"
#include "ui/draw_list.h"

//! Draws a [[ui::DrawList]] on a colour target with one render-graph pass (Phase 15, Day 190).

namespace engine_placeholder::renderer {

/// The texture of an application image, read when the pass is recorded.
///
/// An owner of an image implements this and registers itself with [[renderer::UiRenderer::SetImage]]. The renderer never keeps the handle: it asks for it every frame, so an
/// owner that rebuilds its texture after a device reset (a [[renderer::GpuTexture]], a render target) keeps working.
///
/// Thread-safety: owner-thread-only.
/// Ownership: owns nothing by itself.
class ImageSource {
public:
    virtual ~ImageSource() = default;
    /// Returns the current texture handle, or an invalid handle when the image has no texture now (the commands that name it draw nothing).
    ///
    /// Fails: never.
    virtual rhi::TextureHandle Handle() const = 0;
};

/// An [[renderer::ImageSource]] over a [[renderer::GpuTexture]].
///
/// Thread-safety: owner-thread-only.
/// Ownership: borrows the texture, which must outlive this object.
class GpuTextureImage final : public ImageSource {
public:
    /// Makes a source that reads `texture`.
    /// Param texture: the texture; borrowed.
    explicit GpuTextureImage(const GpuTexture* texture) : texture_(texture) {}
    rhi::TextureHandle Handle() const override { return texture_->Handle(); }

private:
    const GpuTexture* texture_;
};

/// What a [[renderer::UiRenderer]] is made for.
///
/// Thread-safety: thread-compatible plain data.
struct UiRendererDesc {
    /// The format of the colour target the pass draws into; the pipeline is made for it. For an sRGB format the vertex colours are decoded to linear and the hardware encodes
    /// them again, so the UI shows the colours the application wrote.
    rhi::Format color_format = rhi::Format::RGBA8_UNorm;
    /// The most vertices one frame draws; commands that need more are dropped and counted. The vertex memory is 40 bytes per vertex for each frame in flight.
    u32 max_vertices = 1u << 16;
    /// The most indices one frame draws. The index memory is 4 bytes per index for each frame in flight.
    u32 max_indices = 3u << 15;
};

/// Counters of a [[renderer::UiRenderer]], for tests and the benchmark.
///
/// Thread-safety: thread-compatible plain data.
struct UiStats {
    /// Frames that declared the pass.
    u64 passes = 0;
    /// Draw calls recorded since creation.
    u64 draws = 0;
    /// Vertices written since creation.
    u64 vertices = 0;
    /// Commands dropped since creation: past the frame's vertex or index budget, naming an image with no texture, or with an empty clip.
    u64 dropped_commands = 0;
};

/// Draws a draw list: the renderer half of the UI toolkit.
///
/// Per frame: build a [[ui::DrawList]] (a [[ui::DrawListCanvas]]), call [[ui::DrawList::SortLayers]], then [[renderer::UiRenderer::AddToGraph]] after the passes that wrote the
/// target. The pass loads the target (no clear) and blends premultiplied colour over it. One shader draws every primitive; the mode in each vertex says what it is (see
/// `shaders/ui.frag.slang`). Glyph pages come from a [[renderer::GlyphAtlasTexture]] the caller owns (the pass brings its pages up to date before it draws); images come from registered
/// [[renderer::ImageSource]]s.
///
/// Which one to use: [[renderer::TextRenderer]] draws plain text overlays from a layout and owns its own atlas pages (the demos' overlay); this draws everything a UI shows,
/// text included, from one list. A program can have both.
///
/// It registers with a [[renderer::DeviceResources]] at the materials tier; after a reset `Recreate` makes the pipeline and buffers again. The draw list is the caller's and is
/// rebuilt every frame anyway.
///
/// Thread-safety: owner-thread-only.
/// Ownership: borrows the glyph page owner, the draw list given to `AddToGraph` (until the graph is executed) and the registered image sources; owns its GPU objects. The device
///   and the registry must outlive it.
class UiRenderer final : public Recreatable {
public:
    UiRenderer() = default;
    /// Releases everything this object owns.
    ~UiRenderer() override { Destroy(); }
    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;

    /// Creates the pipeline, the buffers and the sampler.
    ///
    /// Param device: the device.
    /// Param pages: the GPU glyph pages the draw list's atlas commands name; borrowed, must outlive this object; may be null for a UI without text.
    /// Param desc: the target format and the budgets.
    /// Param resources: the registry, or null for an object that does not need to survive a reset.
    /// Returns: true on success.
    /// Fails: returns false, logging the reason, if the device has no shader in its format, the format cannot be a colour target or memory runs out; nothing stays allocated.
    /// Aborts: the object already exists, `device` is null, or a budget is 0.
    /// Ownership: the device, the pages and the registry are borrowed.
    bool Create(rhi::Device* device, GlyphAtlasTexture* pages, const UiRendererDesc& desc, DeviceResources* resources);

    /// Destroys everything and forgets the registration.
    void Destroy();

    /// Names the owner of image `id`, replacing an earlier registration.
    ///
    /// Param id: a non-zero [[ui::ImageId]].
    /// Param source: the owner; borrowed until `ClearImage` or `Destroy`; null clears the image.
    /// Aborts: `id` is 0.
    void SetImage(ui::ImageId id, const ImageSource* source);
    /// Forgets image `id`; commands that name it draw nothing.
    ///
    /// Fails: never.
    void ClearImage(ui::ImageId id);

    /// Declares the UI pass in a frame's graph, after the passes that wrote `target`.
    ///
    /// Declares nothing (no pass, no barrier, no cost) when the list has no command. One pass per frame per renderer: the pass reads the list, target and size of the *last* call
    /// before the graph runs, and the per-frame buffers hold one list, so two calls in a frame would draw the second list twice; use one renderer per surface or merge the lists. A format different from the current one rebuilds the pipeline here, on the frame's thread.
    ///
    /// Param graph: the frame's graph.
    /// Param list: the sorted draw list; borrowed until the graph is executed.
    /// Param target: the display target, already written by an earlier pass; accessed as a colour attachment, read and written.
    /// Param format: the target's format.
    /// Param width: the target's width in pixels.
    /// Param height: the target's height in pixels.
    /// Fails: the pass logs and draws nothing if the pipeline for `format` cannot be made.
    /// Aborts: `graph` or `list` is null, `format` is undefined, a size is 0, or the object holds no GPU objects.
    /// Ownership: the pass keeps pointers to this object and to `list` until the graph is executed.
    void AddToGraph(RenderGraph* graph, const ui::DrawList* list, GraphResource target, rhi::Format format, u32 width, u32 height);

    /// Returns the counters.
    ///
    /// Fails: never.
    UiStats Stats() const { return stats_; }

    void Release() override;
    bool Recreate(rhi::Device* device) override;
    bool Live() const override { return device_ != nullptr; }
    const char* RecreatableName() const override { return "ui renderer"; }

private:
    struct ImageEntry {
        ui::ImageId id = 0;
        const ImageSource* source = nullptr;
        bool logged_missing = false;
    };

    bool MakeGpuObjects(rhi::Device* device);
    bool MakePipeline();
    void Record(rhi::CommandList* list);
    static void RecordPass(const PassExecuteContext& context, void* user);

    rhi::Device* device_ = nullptr;
    GlyphAtlasTexture* pages_ = nullptr;
    UiRendererDesc desc_;
    GpuTexture white_;
    GpuSampler sampler_;
    rhi::ShaderHandle vertex_shader_;
    rhi::ShaderHandle fragment_shader_;
    rhi::BindGroupLayoutHandle layout_;
    rhi::PipelineHandle pipeline_;
    rhi::BufferHandle vertex_buffer_;
    rhi::BufferHandle index_buffer_;
    u8* mapped_vertices_ = nullptr;
    u8* mapped_indices_ = nullptr;
    u64 vertex_slot_bytes_ = 0;
    u64 index_slot_bytes_ = 0;
    bool srgb_target_ = false;
    Vector<ImageEntry> images_;
    UiStats stats_;
    const ui::DrawList* pass_list_ = nullptr;
    GraphResource pass_target_;
    u32 pass_width_ = 0;
    u32 pass_height_ = 0;
    bool warned_overflow_ = false;
    DeviceResources* resources_ = nullptr;
    ScopedRegistration registration_;
};

} // namespace engine_placeholder::renderer
