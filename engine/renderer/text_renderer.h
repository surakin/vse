#pragma once

#include "containers/vector.h"
#include "renderer/device_resources.h"
#include "renderer/glyph_atlas_texture.h"
#include "renderer/render_graph.h"
#include "renderer/texture.h"
#include "rhi/rhi.h"
#include "text/glyph_atlas.h"
#include "string_view.h"
#include "text/layout.h"
#include "types.h"

//! Draws laid-out text as textured quads in pixel coordinates, from a glyph atlas, with its own pipeline.

namespace engine_placeholder::renderer
{

/// What a [[renderer::TextRenderer]] is made for.
///
/// - Thread-safety: thread-compatible plain data.
struct TextRendererDesc
{
    /// The format of the color target the text is drawn into; the pipeline is made for it. For an sRGB format the
    /// vertex colors (written as sRGB bytes) are decoded to linear and the hardware encodes them again, so the text
    /// shows the color the application wrote.
    rhi::Format color_format = rhi::Format::RGBA8_UNorm;
    /// The most glyphs one frame draws; further glyphs are dropped and counted. The per-frame vertex memory is 120
    /// bytes per glyph for each frame in flight.
    u32 max_glyphs = 8192;
};

/// Counters of a [[renderer::TextRenderer]], for tests and the benchmark.
///
/// - Thread-safety: thread-compatible plain data.
struct TextStats
{
    /// Glyph quads drawn by `Record` since creation.
    u64 glyphs_drawn = 0;
    /// Glyphs dropped (frame budget or atlas) since creation.
    u64 glyphs_dropped = 0;
    /// Glyphs whose style size is above [[text::kMaxAtlasPixelSize]]: rasterized at the maximum and scaled up.
    u64 scaled_large_glyphs = 0;
    /// Times the render-graph pass was declared (frames that drew text).
    u64 passes = 0;
};

/// Draws text from a [[text::GlyphAtlas]]: add laid-out text each frame, then record it.
///
/// Usage per frame: [[renderer::TextRenderer::BeginFrame]], any number of [[renderer::TextRenderer::Add]], then
/// (between `BeginRendering` and `EndRendering` of a pass that writes the target) [[renderer::TextRenderer::Record]].
/// Positions are pixels of the target with the origin at its top-left corner, X right and Y down, as the layout gives
/// them. Glyph quads land on whole pixels, so the atlas bitmaps are shown texel for texel. The result is premultiplied
/// alpha, blended over what is there.
///
/// It owns a [[renderer::GlyphAtlasTexture]] (the GPU pages), a pipeline, a sampler and per-frame vertex memory, and
/// registers with a [[renderer::DeviceResources]]; after a reset `Recreate` makes them again and the CPU-side quads of
/// the frame in progress are kept. Only SPIR-V is minted for the text shaders so far, so a Direct3D 12 device refuses
/// `Create` with a logged error (`KNOWN-ISSUES.md`).
///
/// - Thread-safety: owner-thread-only, and not safe to use while another thread adds glyphs to the atlas.
/// - Ownership: borrows the atlas (and the fonts the styles name, for the duration of `Add`); owns its GPU objects. The
/// device and the registry must outlive it.
class TextRenderer final : public Recreatable
{
public:
    TextRenderer() = default;
    /// Releases everything this object owns.
    ~TextRenderer() override { Destroy(); }
    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    /// Creates the pipeline, sampler, vertex memory and the GPU atlas pages.
    ///
    /// - Param device: the device.
    /// - Param atlas: the CPU atlas; borrowed, must outlive this object.
    /// - Param desc: the target format and the glyph budget.
    /// - Param resources: the registry, or null for an object that does not need to survive a reset.
    /// - Returns: true on success.
    /// - Fails: returns false, logging the reason, if the device has no shader in its format, the format cannot be a
    /// color target, memory runs out or the atlas textures cannot
    ///   be made; nothing stays allocated.
    /// - Aborts: the object already exists, or `device` or `atlas` is null, or `desc.max_glyphs` is 0.
    /// - Ownership: the device, the atlas and the registry are borrowed.
    bool Create(rhi::Device* device, text::GlyphAtlas* atlas, const TextRendererDesc& desc, DeviceResources* resources);

    /// Destroys everything and forgets the registration.
    void Destroy();

    /// Forgets the quads of the previous frame. Call it once per frame before `Add`.
    void BeginFrame();

    /// Adds the glyphs of a layout to this frame.
    ///
    /// Each glyph is looked up in the atlas (and rasterized into it on first use) at the style's font and size. Glyphs
    /// without pixels (spaces) are skipped.
    ///
    /// - Param layout: the layout of the text, made with `style`'s font and size.
    /// - Param style: the font, size and color (packed RGBA, red in the lowest byte); the font's pixel size is set to
    /// the style's.
    /// - Param x: target pixel the layout's left edge (x = 0) lands on.
    /// - Param y: target pixel the layout's top edge (the first line's top) lands on.
    /// - Returns: true when every glyph was added.
    /// - Fails: returns false, logging once per frame, when the frame's glyph budget or the atlas ran out; the glyphs
    /// that did not fit are counted in
    ///   [[renderer::TextRenderer::DroppedGlyphs]] and the rest are kept.
    /// - Aborts: the style has no open font.
    /// - Ownership: nothing of `layout` or `style` is retained.
    bool Add(const text::TextLayout& layout, const text::TextStyle& style, f32 x, f32 y);

    /// Sends changed atlas pages, writes this frame's quads and records the draws.
    ///
    /// Call it inside a rendering pass whose only color attachment has the format given at creation. Does nothing when
    /// no glyph was added.
    ///
    /// - Param list: the command list that is recording the pass.
    /// - Param target_width: the pass's width in pixels.
    /// - Param target_height: the pass's height in pixels.
    /// - Returns: true when everything was recorded.
    /// - Fails: returns false, logging the reason, if an atlas page cannot be sent (nothing is drawn) or a bind group
    /// cannot be made; whatever was recorded stays valid.
    /// - Aborts: the object does not hold its GPU objects, the call is outside a frame, or it is the second call in one
    /// frame (the frame's vertex region is written once).
    /// - Ownership: borrows the list for the call.
    bool Record(rhi::CommandList* list, u32 target_width, u32 target_height);

    /// Lays out `utf8` and adds it: the convenience call a program makes each frame.
    ///
    /// The layout is kept in the renderer and reused, so a steady frame allocates nothing per glyph.
    ///
    /// - Param utf8: the text.
    /// - Param style: font, size and color.
    /// - Param options: wrapping, line height, tabs, alignment.
    /// - Param x: target pixel of the layout's left edge.
    /// - Param y: target pixel of the layout's top edge.
    /// - Returns: true when the text was laid out and every glyph was added.
    /// - Fails: returns false, as [[text::Layout]] and [[renderer::TextRenderer::Add]] fail.
    /// - Aborts: the style has no open font.
    /// - Ownership: nothing is retained.
    bool Draw(StringView utf8, const text::TextStyle& style, const text::LayoutOptions& options, f32 x, f32 y);

    /// Declares the text pass in a frame's graph, after the passes that wrote `target`.
    ///
    /// Declares nothing (no pass, no barrier, no cost) when no glyph was added this frame. Otherwise the pass blends
    /// the quads into `target` without clearing it. A format different from the current one rebuilds the pipeline here,
    /// on the frame's thread.
    ///
    /// - Param graph: the frame's graph.
    /// - Param target: the display target, already written by an earlier pass (the tonemap); accessed as a color
    /// attachment, read and written.
    /// - Param format: the target's format.
    /// - Param width: the target's width in pixels.
    /// - Param height: the target's height in pixels.
    /// - Fails: the pass logs and draws nothing if the pipeline for `format` cannot be made.
    /// - Aborts: `graph` is null, `format` is undefined, a size is 0, or the object holds no GPU objects.
    /// - Ownership: the pass keeps a pointer to this object until the graph is executed.
    void AddToGraph(RenderGraph* graph, GraphResource target, rhi::Format format, u32 width, u32 height);

    /// Returns the counters.
    /// - Fails: never.
    TextStats Stats() const;

    /// Returns the glyph quads waiting for `Record`.
    /// - Fails: never.
    u32 QueuedGlyphs() const { return queued_; }
    /// Returns the glyphs dropped since creation (budget or atlas).
    /// - Fails: never.
    u64 DroppedGlyphs() const { return dropped_; }
    /// Returns the GPU atlas pages.
    /// - Fails: never.
    const GlyphAtlasTexture& Pages() const { return pages_; }

    void Release() override;
    bool Recreate(rhi::Device* device) override;
    bool Live() const override { return device_ != nullptr; }
    const char* RecreatableName() const override { return "text renderer"; }

private:
    struct Vertex
    {
        f32 x;
        f32 y;
        f32 u;
        f32 v;
        u32 color;
    };
    bool MakeGpuObjects(rhi::Device* device);
    bool MakePipeline();
    static void RecordPass(const PassExecuteContext& context, void* user);

    rhi::Device* device_ = nullptr;
    text::GlyphAtlas* atlas_ = nullptr;
    TextRendererDesc desc_;
    GlyphAtlasTexture pages_;
    GpuSampler sampler_;
    rhi::ShaderHandle vertex_shader_;
    rhi::ShaderHandle fragment_shader_;
    rhi::BindGroupLayoutHandle layout_;
    rhi::PipelineHandle pipeline_;
    rhi::BufferHandle vertex_buffer_;
    u8* mapped_ = nullptr;
    u64 slot_bytes_ = 0;
    bool srgb_target_ = false;
    Vector<Vector<Vertex>> batches_;  // one list of quads (6 vertices each) per atlas page
    u32 queued_ = 0;
    u64 dropped_ = 0;
    u64 drawn_ = 0;
    u64 scaled_large_ = 0;
    u64 passes_ = 0;
    Vector<u32> logged_large_sizes_;  // the sizes above the atlas maximum already reported in the log
    text::TextLayout scratch_layout_;
    GraphResource pass_target_;
    u32 pass_width_ = 0;
    u32 pass_height_ = 0;
    bool warned_this_frame_ = false;
    u64 recorded_frame_ = 0;     // frame number + 1 of the last Record that drew
    Vector<u32> first_scratch_;  // Record's per-page first vertex, kept between frames
    ScopedRegistration registration_;
};

}  // namespace engine_placeholder::renderer
