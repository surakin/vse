#include "lang/new.h"
#include "renderer/text_renderer.h"

#include "log/assert.h"
#include "renderer/renderer_log.h"
#include "rhi/uniform_ring.h" // kSlots: the frames a device keeps in flight
#include "text.frag.dxil.inc" // and its .spv.inc
#include "text.vert.refl.inc"
#include "text.vert.dxil.inc" // and its .spv.inc
#include "cstr/mem_ops.h"

namespace engine_placeholder::renderer {

namespace {

// The push constants of shaders/text.vert (two floats, a flag and padding).
struct TextParams {
    f32 pixel_to_clip[2];
    f32 srgb_target;
    f32 pad;
};
// The expected values come from the shader's reflection, so changing the block in text.vert.slang without changing this struct fails the build.
static_assert(sizeof(TextParams) == shaders::kTextVertParamsSize && ENGINE_OFFSET_OF(TextParams, pixel_to_clip) == shaders::kTextVertParamsPixelToClipOffset &&
                  ENGINE_OFFSET_OF(TextParams, srgb_target) == shaders::kTextVertParamsSrgbTargetOffset && ENGINE_OFFSET_OF(TextParams, pad) == shaders::kTextVertParamsPadOffset,
              "TextParams must match the std140 block in shaders/text.vert.slang");

constexpr u32 kVerticesPerGlyph = 6;

} // namespace

bool TextRenderer::Create(rhi::Device* device, text::GlyphAtlas* atlas, const TextRendererDesc& desc, DeviceResources* resources) {
    ENGINE_ASSERT(device != nullptr && atlas != nullptr && device_ == nullptr && desc.max_glyphs > 0, "TextRenderer::Create: bad arguments or the object exists");
    atlas_ = atlas;
    desc_ = desc;
    srgb_target_ = rhi::FormatInfo(desc.color_format).is_srgb;
    if (!pages_.Create(device, atlas, resources)) {
        atlas_ = nullptr;
        return false;
    }
    rhi::SamplerDesc sd;
    sd.address_u = sd.address_v = sd.address_w = rhi::AddressMode::ClampToEdge;
    sd.mip_filter = rhi::FilterMode::Nearest;
    if (!sampler_.Create(device, sd, resources, "text sampler") || !MakeGpuObjects(device)) {
        Destroy();
        return false;
    }
    if (resources != nullptr && !registration_.Attach(resources, this, tiers::kMaterials)) {
        Destroy();
        return false;
    }
    return true;
}

bool TextRenderer::MakeGpuObjects(rhi::Device* device) {
    device_ = device;
    vertex_shader_ = rhi::CreateShaderFromSet(*device, shaders::kTextVertShader, rhi::ShaderStage::Vertex);
    fragment_shader_ = rhi::CreateShaderFromSet(*device, shaders::kTextFragShader, rhi::ShaderStage::Fragment);
    bool ok = vertex_shader_.IsValid() && fragment_shader_.IsValid();
    if (ok) {
        rhi::BindingDesc bindings[2] = {{0, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment, 1},
                                        {1, rhi::BindingType::Sampler, rhi::ShaderStage::Fragment, 1}};
        rhi::BindGroupLayoutDesc ld;
        ld.entries = rhi::Span<rhi::BindingDesc>(bindings, 2);
        ld.debug_name = "text";
        layout_ = device->CreateBindGroupLayout(ld);
        ok = layout_.IsValid();
    }
    if (ok) {
        ok = MakePipeline();
    }
    if (ok) {
        slot_bytes_ = static_cast<u64>(desc_.max_glyphs) * kVerticesPerGlyph * sizeof(Vertex);
        rhi::BufferDesc bd;
        bd.size = slot_bytes_ * rhi::UniformRing::kSlots;
        bd.usage = rhi::BufferUsage::Vertex;
        bd.location = rhi::MemoryLocation::CpuToGpu;
        bd.debug_name = "text vertices";
        vertex_buffer_ = device->CreateBuffer(bd);
        ok = vertex_buffer_.IsValid();
        if (ok) {
            mapped_ = static_cast<u8*>(device->MapBuffer(vertex_buffer_));
            ok = mapped_ != nullptr;
        }
    }
    if (!ok) {
        RenderLog(LogVerbosity::Error, "TextRenderer: the GPU objects could not be made");
        Release();
    }
    return ok;
}

bool TextRenderer::MakePipeline() {
    rhi::VertexBindingDesc binding = {sizeof(Vertex), false};
    rhi::VertexAttributeDesc attributes[3] = {{0, 0, rhi::VertexFormat::Float2, ENGINE_OFFSET_OF(Vertex, x)},
                                              {1, 0, rhi::VertexFormat::Float2, ENGINE_OFFSET_OF(Vertex, u)},
                                              {2, 0, rhi::VertexFormat::UNorm8x4, ENGINE_OFFSET_OF(Vertex, color)}};
    rhi::GraphicsPipelineDesc gp;
    gp.vertex_shader = vertex_shader_;
    gp.fragment_shader = fragment_shader_;
    gp.vertex_bindings = rhi::Span<rhi::VertexBindingDesc>(&binding, 1);
    gp.vertex_attributes = rhi::Span<rhi::VertexAttributeDesc>(attributes, 3);
    gp.cull_mode = rhi::CullMode::None;
    gp.color_targets[0].format = desc_.color_format;
    gp.color_targets[0].blend.enable = true; // premultiplied alpha: the shader multiplies the color by the coverage
    gp.color_targets[0].blend.src_color = rhi::BlendFactor::One;
    gp.color_targets[0].blend.dst_color = rhi::BlendFactor::OneMinusSrcAlpha;
    gp.color_targets[0].blend.src_alpha = rhi::BlendFactor::One;
    gp.color_targets[0].blend.dst_alpha = rhi::BlendFactor::OneMinusSrcAlpha;
    gp.color_target_count = 1;
    gp.bind_group_layouts[0] = layout_;
    gp.bind_group_layout_count = 1;
    gp.push_constant_bytes = sizeof(TextParams);
    gp.push_constant_stages = rhi::ShaderStage::Vertex;
    gp.debug_name = "text";
    pipeline_ = device_->CreateGraphicsPipeline(gp);
    srgb_target_ = rhi::FormatInfo(desc_.color_format).is_srgb;
    return pipeline_.IsValid();
}

void TextRenderer::Destroy() {
    registration_.Reset();
    Release();
    sampler_.Destroy();
    pages_.Destroy();
    atlas_ = nullptr;
    batches_.Clear();
    queued_ = 0;
    dropped_ = 0;
    drawn_ = 0;
    scaled_large_ = 0;
    passes_ = 0;
    logged_large_sizes_.Clear();
}

void TextRenderer::Release() {
    if (device_ != nullptr) {
        if (mapped_ != nullptr) device_->UnmapBuffer(vertex_buffer_);
        if (vertex_buffer_.IsValid()) device_->DestroyBuffer(vertex_buffer_);
        if (pipeline_.IsValid()) device_->DestroyPipeline(pipeline_);
        if (layout_.IsValid()) device_->DestroyBindGroupLayout(layout_);
        if (vertex_shader_.IsValid()) device_->DestroyShader(vertex_shader_);
        if (fragment_shader_.IsValid()) device_->DestroyShader(fragment_shader_);
    }
    vertex_buffer_ = {};
    pipeline_ = {};
    layout_ = {};
    vertex_shader_ = {};
    fragment_shader_ = {};
    mapped_ = nullptr;
    device_ = nullptr;
    recorded_frame_ = 0; // a new device counts frames from the start again
}

bool TextRenderer::Recreate(rhi::Device* device) {
    ENGINE_ASSERT(device_ == nullptr && atlas_ != nullptr, "TextRenderer::Recreate: the GPU objects still exist or there is no atlas");
    return MakeGpuObjects(device);
}

void TextRenderer::BeginFrame() {
    for (usize i = 0; i < batches_.Size(); ++i) {
        batches_[i].Clear();
    }
    queued_ = 0;
    warned_this_frame_ = false;
}

bool TextRenderer::Add(const text::TextLayout& layout, const text::TextStyle& style, f32 x, f32 y) {
    ENGINE_ASSERT(style.font != nullptr && style.font->IsOpen(), "TextRenderer::Add: the style has no open font");
    ENGINE_ASSERT(atlas_ != nullptr, "TextRenderer::Add: the object does not exist");
    // Above the atlas maximum the glyph is rasterized at the maximum and its quad is scaled up (blurry but present and placed by the layout, which ran at the
    // requested size) until Phase 17 draws such text as outline meshes.
    const bool large = style.pixel_size > text::kMaxAtlasPixelSize;
    const u32 raster_size = large ? text::kMaxAtlasPixelSize : style.pixel_size;
    const f32 scale = large ? static_cast<f32>(style.pixel_size) / static_cast<f32>(text::kMaxAtlasPixelSize) : 1.0f;
    bool first_use = false;
    if (large) {
        first_use = true;
        for (usize i = 0; i < logged_large_sizes_.Size(); ++i) {
            first_use = first_use && logged_large_sizes_[i] != style.pixel_size;
        }
    }
    if (first_use) {
        logged_large_sizes_.Push(style.pixel_size); // once per size, however often the sizes alternate
        RenderLog(LogVerbosity::Info, "TextRenderer: %u px text is above the atlas maximum of %u px; its glyphs are scaled up from the maximum", style.pixel_size,
                  static_cast<u32>(text::kMaxAtlasPixelSize));
    }
    if (!style.font->SetPixelSize(raster_size).Ok()) {
        return false;
    }
    bool all = true;
    for (usize i = 0; i < layout.glyphs.Size(); ++i) {
        const text::PositionedGlyph& g = layout.glyphs[i];
        text::GlyphEntry entry;
        if (!atlas_->GetOrAdd(*style.font, g.glyph, &entry).Ok() || !entry.valid) {
            ++dropped_;
            all = false;
            continue;
        }
        if (!entry.has_pixels) {
            continue;
        }
        if (queued_ >= desc_.max_glyphs) {
            ++dropped_;
            all = false;
            continue;
        }
        while (batches_.Size() <= entry.page) {
            batches_.Push(Vector<Vertex>());
        }
        // Whole-pixel corners: the bitmap was rasterized at an integer origin, so this shows it texel for texel.
        const text::GlyphQuad q = text::PlaceGlyph(x, y, g.x, g.y, entry, scale);
        const f32 left = q.left;
        const f32 top = q.top;
        const f32 right = q.right;
        const f32 bottom = q.bottom;
        const Vertex tl = {left, top, entry.u0, entry.v0, style.color};
        const Vertex tr = {right, top, entry.u1, entry.v0, style.color};
        const Vertex bl = {left, bottom, entry.u0, entry.v1, style.color};
        const Vertex br = {right, bottom, entry.u1, entry.v1, style.color};
        Vector<Vertex>& batch = batches_[entry.page];
        batch.Push(tl);
        batch.Push(bl);
        batch.Push(tr);
        batch.Push(tr);
        batch.Push(bl);
        batch.Push(br);
        ++queued_;
        if (large) ++scaled_large_;
    }
    if (!all && !warned_this_frame_) {
        warned_this_frame_ = true;
        RenderLog(LogVerbosity::Warn, "TextRenderer: glyphs were dropped (budget %u per frame, or the atlas is full); %llu so far", desc_.max_glyphs,
                  static_cast<unsigned long long>(dropped_));
    }
    return all;
}

bool TextRenderer::Draw(StringView utf8, const text::TextStyle& style, const text::LayoutOptions& options, f32 x, f32 y) {
    ENGINE_ASSERT(style.font != nullptr && style.font->IsOpen(), "TextRenderer::Draw: the style has no open font");
    if (!text::Layout(utf8, style, options, &scratch_layout_).Ok()) {
        return false;
    }
    return Add(scratch_layout_, style, x, y);
}

TextStats TextRenderer::Stats() const {
    TextStats stats;
    stats.glyphs_drawn = drawn_;
    stats.glyphs_dropped = dropped_;
    stats.scaled_large_glyphs = scaled_large_;
    stats.passes = passes_;
    return stats;
}

void TextRenderer::AddToGraph(RenderGraph* graph, GraphResource target, rhi::Format format, u32 width, u32 height) {
    ENGINE_ASSERT(graph != nullptr && format != rhi::Format::Undefined && width > 0 && height > 0 && device_ != nullptr,
                  "TextRenderer::AddToGraph: bad arguments or the object holds no GPU objects");
    if (queued_ == 0) {
        return; // nothing to draw: no pass, no barrier
    }
    if (format != desc_.color_format || !pipeline_.IsValid()) {
        // The target's format changed (a settings change), or an earlier rebuild failed: the pipeline follows the format, built here and not inside the recording callback.
        if (pipeline_.IsValid()) device_->DestroyPipeline(pipeline_);
        pipeline_ = {};
        desc_.color_format = format;
        MakePipeline();
    }
    pass_target_ = target;
    pass_width_ = width;
    pass_height_ = height;
    ++passes_;
    graph->AddPass("text", &TextRenderer::RecordPass, this).ReadWrite(target, GraphUsage::ColorAttachment);
}

void TextRenderer::RecordPass(const PassExecuteContext& context, void* user) {
    TextRenderer* self = static_cast<TextRenderer*>(user);
    if (!self->pipeline_.IsValid()) {
        RenderLog(LogVerbosity::Error, "TextRenderer: the pipeline for the target format could not be made; no text is drawn");
        return;
    }
    rhi::RenderingDesc rd;
    rd.color_count = 1;
    rd.color[0].texture = context.graph->IsImport(self->pass_target_) ? context.graph->ImportedTexture(self->pass_target_) : context.graph->ResolvedTexture(self->pass_target_);
    rd.color[0].load = rhi::LoadOp::Load;
    context.list->BeginRendering(rd);
    self->Record(context.list, self->pass_width_, self->pass_height_);
    context.list->EndRendering();
}

bool TextRenderer::Record(rhi::CommandList* list, u32 target_width, u32 target_height) {
    ENGINE_ASSERT(device_ != nullptr && device_->InFrame(), "TextRenderer::Record: no GPU objects, or the call is outside a frame");
    ENGINE_ASSERT(target_width > 0 && target_height > 0, "TextRenderer::Record: the target is empty");
    if (queued_ == 0) {
        return true;
    }
    if (!pages_.Sync()) {
        return false; // a page that is missing or was never sent must not be sampled
    }
    ENGINE_ASSERT(recorded_frame_ != device_->FrameNumber() + 1, "TextRenderer::Record: called twice in one frame (the frame's vertex region would be overwritten)");
    recorded_frame_ = device_->FrameNumber() + 1;
    bool ok = true;
    // Write the quads of every page one after another into this frame's slot of the vertex buffer.
    const u32 slot = static_cast<u32>(device_->FrameNumber() % rhi::UniformRing::kSlots);
    const u64 slot_offset = slot_bytes_ * slot;
    u64 written = 0; // vertices
    Vector<u32> first;
    first.Reserve(batches_.Size());
    for (usize page = 0; page < batches_.Size(); ++page) {
        first.Push(static_cast<u32>(written));
        const Vector<Vertex>& batch = batches_[page];
        if (batch.Size() > 0) {
            MemCopy(mapped_ + slot_offset + written * sizeof(Vertex), batch.Data(), batch.Size() * sizeof(Vertex));
            written += batch.Size();
        }
    }
    device_->FlushMappedBuffer(vertex_buffer_, slot_offset, written * sizeof(Vertex));
    drawn_ += queued_;

    TextParams params;
    params.pixel_to_clip[0] = 2.0f / static_cast<f32>(target_width);
    params.pixel_to_clip[1] = 2.0f / static_cast<f32>(target_height);
    params.srgb_target = srgb_target_ ? 1.0f : 0.0f;
    params.pad = 0.0f;

    list->SetPipeline(pipeline_);
    list->SetViewport(rhi::Viewport{0, 0, static_cast<f32>(target_width), static_cast<f32>(target_height), 0.0f, 1.0f});
    list->SetScissor(rhi::ScissorRect{0, 0, target_width, target_height});
    list->SetPushConstants(0, &params, sizeof(params));
    list->SetVertexBuffer(0, vertex_buffer_, slot_offset);
    for (usize page = 0; page < batches_.Size(); ++page) {
        if (batches_[page].Size() == 0) {
            continue;
        }
        rhi::BindGroupEntry entries[2];
        entries[0].binding = 0;
        entries[0].texture = pages_.Page(static_cast<u32>(page));
        entries[1].binding = 1;
        entries[1].sampler = sampler_.Handle();
        rhi::BindGroupDesc gd;
        gd.layout = layout_;
        gd.entries = rhi::Span<rhi::BindGroupEntry>(entries, 2);
        gd.transient = true;
        const rhi::BindGroupHandle group = device_->CreateBindGroup(gd);
        if (!group.IsValid()) {
            ok = false;
            continue;
        }
        list->SetBindGroup(0, group);
        list->Draw(static_cast<u32>(batches_[page].Size()), 1, first[page], 0);
    }
    return ok;
}

} // namespace engine_placeholder::renderer
