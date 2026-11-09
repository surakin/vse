#include "renderer/ui_renderer.h"

#include <cstddef>
#include <cstring>

#include "log/assert.h"
#include "renderer/renderer_log.h"
#include "rhi/uniform_ring.h" // kSlots: the frames a device keeps in flight
#include "ui.frag.spv.inc"
#include "ui.vert.refl.inc"
#include "ui.vert.spv.inc"

namespace engine_placeholder::renderer {

namespace {

// The push constants of shaders/ui.vert (two floats, a flag and padding).
struct UiParams {
    f32 pixel_to_clip[2];
    f32 srgb_target;
    f32 pad;
};
static_assert(sizeof(UiParams) == shaders::kUiVertParamsSize && offsetof(UiParams, pixel_to_clip) == shaders::kUiVertParamsPixelToClipOffset &&
                  offsetof(UiParams, srgb_target) == shaders::kUiVertParamsSrgbTargetOffset && offsetof(UiParams, pad) == shaders::kUiVertParamsPadOffset,
              "UiParams must match the std140 block in shaders/ui.vert.slang");
static_assert(sizeof(ui::Vertex) == 40 && offsetof(ui::Vertex, color) == 16 && offsetof(ui::Vertex, half_w) == 20 && offsetof(ui::Vertex, flags) == 36,
              "the vertex attributes below follow ui::Vertex");

// Only SPIR-V is minted for the UI shaders so far (Direct3D 12 waits for Day 205); a D3D12 device finds no blob and says so.
const rhi::ShaderBlobSet kUiVertSet = {"ui.vert", rhi::SpirvBlob(shaders::kUiVertSpv, shaders::kUiVertSpvWordCount), {}};
const rhi::ShaderBlobSet kUiFragSet = {"ui.frag", rhi::SpirvBlob(shaders::kUiFragSpv, shaders::kUiFragSpvWordCount), {}};

bool MakeWhite(rhi::Device* device, GpuTexture* texture, DeviceResources* resources) {
    TextureImage image;
    image.desc.width = 1;
    image.desc.height = 1;
    image.desc.format = rhi::Format::RGBA8_UNorm;
    image.desc.usage = rhi::TextureUsage::Sampled;
    image.pixels.Resize(4);
    for (usize i = 0; i < 4; ++i) image.pixels[i] = 255;
    return texture->Create(device, static_cast<TextureImage&&>(image), resources, "ui white");
}

} // namespace

bool UiRenderer::Create(rhi::Device* device, GlyphAtlasTexture* pages, const UiRendererDesc& desc, DeviceResources* resources) {
    ENGINE_ASSERT(device != nullptr && device_ == nullptr && desc.max_vertices > 0 && desc.max_indices > 0, "UiRenderer::Create: bad arguments or the object exists");
    pages_ = pages;
    desc_ = desc;
    resources_ = resources;
    srgb_target_ = rhi::FormatInfo(desc.color_format).is_srgb;
    rhi::SamplerDesc sd;
    sd.address_u = sd.address_v = sd.address_w = rhi::AddressMode::ClampToEdge;
    sd.mip_filter = rhi::FilterMode::Nearest;
    if (!MakeWhite(device, &white_, resources) || !sampler_.Create(device, sd, resources, "ui sampler") || !MakeGpuObjects(device)) {
        Destroy();
        return false;
    }
    if (resources != nullptr && !registration_.Attach(resources, this, tiers::kMaterials)) {
        Destroy();
        return false;
    }
    return true;
}

bool UiRenderer::MakeGpuObjects(rhi::Device* device) {
    device_ = device;
    vertex_shader_ = rhi::CreateShaderFromSet(*device, kUiVertSet, rhi::ShaderStage::Vertex);
    fragment_shader_ = rhi::CreateShaderFromSet(*device, kUiFragSet, rhi::ShaderStage::Fragment);
    bool ok = vertex_shader_.IsValid() && fragment_shader_.IsValid();
    if (ok) {
        rhi::BindingDesc bindings[2] = {{0, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment, 1},
                                        {1, rhi::BindingType::Sampler, rhi::ShaderStage::Fragment, 1}};
        rhi::BindGroupLayoutDesc ld;
        ld.entries = rhi::Span<rhi::BindingDesc>(bindings, 2);
        ld.debug_name = "ui";
        layout_ = device->CreateBindGroupLayout(ld);
        ok = layout_.IsValid();
    }
    if (ok) ok = MakePipeline();
    if (ok) {
        vertex_slot_bytes_ = static_cast<u64>(desc_.max_vertices) * sizeof(ui::Vertex);
        index_slot_bytes_ = static_cast<u64>(desc_.max_indices) * sizeof(u32);
        rhi::BufferDesc bd;
        bd.size = vertex_slot_bytes_ * rhi::UniformRing::kSlots;
        bd.usage = rhi::BufferUsage::Vertex;
        bd.location = rhi::MemoryLocation::CpuToGpu;
        bd.debug_name = "ui vertices";
        vertex_buffer_ = device->CreateBuffer(bd);
        bd.size = index_slot_bytes_ * rhi::UniformRing::kSlots;
        bd.usage = rhi::BufferUsage::Index;
        bd.debug_name = "ui indices";
        index_buffer_ = device->CreateBuffer(bd);
        ok = vertex_buffer_.IsValid() && index_buffer_.IsValid();
        if (ok) {
            mapped_vertices_ = static_cast<u8*>(device->MapBuffer(vertex_buffer_));
            mapped_indices_ = static_cast<u8*>(device->MapBuffer(index_buffer_));
            ok = mapped_vertices_ != nullptr && mapped_indices_ != nullptr;
        }
    }
    if (!ok) {
        RenderLog(LogVerbosity::Error, "UiRenderer: the GPU objects could not be made");
        Release();
    }
    return ok;
}

bool UiRenderer::MakePipeline() {
    rhi::VertexBindingDesc binding = {sizeof(ui::Vertex), false};
    rhi::VertexAttributeDesc attributes[5] = {{0, 0, rhi::VertexFormat::Float2, offsetof(ui::Vertex, x)},
                                              {1, 0, rhi::VertexFormat::Float2, offsetof(ui::Vertex, u)},
                                              {2, 0, rhi::VertexFormat::UNorm8x4, offsetof(ui::Vertex, color)},
                                              {3, 0, rhi::VertexFormat::Float4, offsetof(ui::Vertex, half_w)},
                                              {4, 0, rhi::VertexFormat::UInt1, offsetof(ui::Vertex, flags)}};
    rhi::GraphicsPipelineDesc gp;
    gp.vertex_shader = vertex_shader_;
    gp.fragment_shader = fragment_shader_;
    gp.vertex_bindings = rhi::Span<rhi::VertexBindingDesc>(&binding, 1);
    gp.vertex_attributes = rhi::Span<rhi::VertexAttributeDesc>(attributes, 5);
    gp.cull_mode = rhi::CullMode::None;
    gp.color_targets[0].format = desc_.color_format;
    gp.color_targets[0].blend.enable = true; // premultiplied alpha: the shader multiplies the colour by the coverage
    gp.color_targets[0].blend.src_color = rhi::BlendFactor::One;
    gp.color_targets[0].blend.dst_color = rhi::BlendFactor::OneMinusSrcAlpha;
    gp.color_targets[0].blend.src_alpha = rhi::BlendFactor::One;
    gp.color_targets[0].blend.dst_alpha = rhi::BlendFactor::OneMinusSrcAlpha;
    gp.color_target_count = 1;
    gp.bind_group_layouts[0] = layout_;
    gp.bind_group_layout_count = 1;
    gp.push_constant_bytes = sizeof(UiParams);
    gp.push_constant_stages = rhi::ShaderStage::Vertex;
    gp.debug_name = "ui";
    pipeline_ = device_->CreateGraphicsPipeline(gp);
    srgb_target_ = rhi::FormatInfo(desc_.color_format).is_srgb;
    return pipeline_.IsValid();
}

void UiRenderer::Destroy() {
    registration_.Reset();
    Release();
    sampler_.Destroy();
    white_.Destroy();
    pages_ = nullptr;
    resources_ = nullptr;
    images_.Clear();
    stats_ = UiStats();
    pass_list_ = nullptr;
}

void UiRenderer::Release() {
    if (device_ != nullptr) {
        if (mapped_vertices_ != nullptr) device_->UnmapBuffer(vertex_buffer_);
        if (mapped_indices_ != nullptr) device_->UnmapBuffer(index_buffer_);
        if (vertex_buffer_.IsValid()) device_->DestroyBuffer(vertex_buffer_);
        if (index_buffer_.IsValid()) device_->DestroyBuffer(index_buffer_);
        if (pipeline_.IsValid()) device_->DestroyPipeline(pipeline_);
        if (layout_.IsValid()) device_->DestroyBindGroupLayout(layout_);
        if (vertex_shader_.IsValid()) device_->DestroyShader(vertex_shader_);
        if (fragment_shader_.IsValid()) device_->DestroyShader(fragment_shader_);
    }
    vertex_buffer_ = {};
    index_buffer_ = {};
    pipeline_ = {};
    layout_ = {};
    vertex_shader_ = {};
    fragment_shader_ = {};
    mapped_vertices_ = nullptr;
    mapped_indices_ = nullptr;
    device_ = nullptr;
}

bool UiRenderer::Recreate(rhi::Device* device) {
    ENGINE_ASSERT(device_ == nullptr, "UiRenderer::Recreate: the GPU objects still exist");
    return MakeGpuObjects(device);
}

void UiRenderer::SetImage(ui::ImageId id, const ImageSource* source) {
    ENGINE_ASSERT(id != 0, "UiRenderer::SetImage: image 0 means no image");
    for (usize i = 0; i < images_.Size(); ++i) {
        if (images_[i].id == id) {
            if (source == nullptr) {
                images_.Remove(i);
            } else {
                images_[i].source = source;
                images_[i].logged_missing = false;
            }
            return;
        }
    }
    if (source != nullptr) {
        ImageEntry entry;
        entry.id = id;
        entry.source = source;
        images_.Push(entry);
    }
}

void UiRenderer::ClearImage(ui::ImageId id) { SetImage(id, nullptr); }

void UiRenderer::AddToGraph(RenderGraph* graph, const ui::DrawList* list, GraphResource target, rhi::Format format, u32 width, u32 height) {
    ENGINE_ASSERT(graph != nullptr && list != nullptr && format != rhi::Format::Undefined && width > 0 && height > 0 && device_ != nullptr,
                  "UiRenderer::AddToGraph: bad arguments or the object holds no GPU objects");
    if (list->commands.Empty()) {
        return; // nothing to draw: no pass, no barrier
    }
    if (format != desc_.color_format || !pipeline_.IsValid()) {
        // The target's format changed, or an earlier rebuild failed: the pipeline follows the format, built here and not inside the recording callback.
        if (pipeline_.IsValid()) device_->DestroyPipeline(pipeline_);
        pipeline_ = {};
        desc_.color_format = format;
        MakePipeline();
    }
    pass_list_ = list;
    pass_target_ = target;
    pass_width_ = width;
    pass_height_ = height;
    ++stats_.passes;
    graph->AddPass("ui", &UiRenderer::RecordPass, this).ReadWrite(target, GraphUsage::ColorAttachment);
}

void UiRenderer::RecordPass(const PassExecuteContext& context, void* user) {
    UiRenderer* self = static_cast<UiRenderer*>(user);
    if (!self->pipeline_.IsValid()) {
        RenderLog(LogVerbosity::Error, "UiRenderer: the pipeline for the target format could not be made; no UI is drawn");
        return;
    }
    rhi::RenderingDesc rd;
    rd.color_count = 1;
    rd.color[0].texture = context.graph->IsImport(self->pass_target_) ? context.graph->ImportedTexture(self->pass_target_) : context.graph->ResolvedTexture(self->pass_target_);
    rd.color[0].load = rhi::LoadOp::Load;
    context.list->BeginRendering(rd);
    self->Record(context.list);
    context.list->EndRendering();
}

void UiRenderer::Record(rhi::CommandList* list) {
    ENGINE_ASSERT(device_ != nullptr && device_->InFrame(), "UiRenderer::Record: no GPU objects, or the call is outside a frame");
    const ui::DrawList& dl = *pass_list_;
    const usize vertex_count = dl.vertices.Size() < desc_.max_vertices ? dl.vertices.Size() : desc_.max_vertices;
    const usize index_count = dl.indices.Size() < desc_.max_indices ? dl.indices.Size() : desc_.max_indices;
    const bool truncated = vertex_count < dl.vertices.Size() || index_count < dl.indices.Size();
    if (truncated && !warned_overflow_) {
        warned_overflow_ = true;
        RenderLog(LogVerbosity::Warn, "UiRenderer: the draw list (%llu vertices, %llu indices) is above the budget (%u, %u); the commands that do not fit are dropped",
                  static_cast<unsigned long long>(dl.vertices.Size()), static_cast<unsigned long long>(dl.indices.Size()), desc_.max_vertices, desc_.max_indices);
    }
    if (pages_ != nullptr && !pages_->Sync()) {
        RenderLog(LogVerbosity::Error, "UiRenderer: the glyph pages could not be brought up to date; no UI is drawn");
        return;
    }
    const u32 slot = static_cast<u32>(device_->FrameNumber() % rhi::UniformRing::kSlots);
    const u64 vertex_offset = vertex_slot_bytes_ * slot;
    const u64 index_offset = index_slot_bytes_ * slot;
    if (vertex_count > 0) std::memcpy(mapped_vertices_ + vertex_offset, dl.vertices.Data(), vertex_count * sizeof(ui::Vertex));
    if (index_count > 0) std::memcpy(mapped_indices_ + index_offset, dl.indices.Data(), index_count * sizeof(u32));
    device_->FlushMappedBuffer(vertex_buffer_, vertex_offset, vertex_count * sizeof(ui::Vertex));
    device_->FlushMappedBuffer(index_buffer_, index_offset, index_count * sizeof(u32));
    stats_.vertices += vertex_count;

    UiParams params;
    params.pixel_to_clip[0] = 2.0f / static_cast<f32>(pass_width_);
    params.pixel_to_clip[1] = 2.0f / static_cast<f32>(pass_height_);
    params.srgb_target = srgb_target_ ? 1.0f : 0.0f;
    params.pad = 0.0f;

    list->SetPipeline(pipeline_);
    list->SetViewport(rhi::Viewport{0, 0, static_cast<f32>(pass_width_), static_cast<f32>(pass_height_), 0.0f, 1.0f});
    list->SetPushConstants(0, &params, sizeof(params));
    list->SetVertexBuffer(0, vertex_buffer_, vertex_offset);
    list->SetIndexBuffer(index_buffer_, index_offset, rhi::IndexType::UInt32);

    rhi::TextureHandle bound_texture;
    rhi::BindGroupHandle group;
    for (usize c = 0; c < dl.commands.Size(); ++c) {
        const ui::DrawCommand& cmd = dl.commands[c];
        // Past the budget: the command's indices must be inside the copied range, and every vertex they name too.
        bool fits = static_cast<u64>(cmd.first_index) + cmd.index_count <= index_count;
        if (fits && truncated) {
            for (u32 i = 0; i < cmd.index_count && fits; ++i) fits = dl.indices[cmd.first_index + i] < vertex_count;
        }
        // The clip, clamped to the target.
        const i64 x0 = cmd.clip.x < 0 ? 0 : cmd.clip.x;
        const i64 y0 = cmd.clip.y < 0 ? 0 : cmd.clip.y;
        const i64 x1 = static_cast<i64>(cmd.clip.x) + cmd.clip.w < pass_width_ ? static_cast<i64>(cmd.clip.x) + cmd.clip.w : pass_width_;
        const i64 y1 = static_cast<i64>(cmd.clip.y) + cmd.clip.h < pass_height_ ? static_cast<i64>(cmd.clip.y) + cmd.clip.h : pass_height_;
        // The texture the command samples.
        rhi::TextureHandle texture = white_.Handle();
        if (cmd.texture.kind == ui::TextureKind::Atlas) {
            texture = (pages_ != nullptr && cmd.texture.id < pages_->PageCount()) ? pages_->Page(cmd.texture.id) : rhi::TextureHandle();
        } else if (cmd.texture.kind == ui::TextureKind::Image) {
            texture = rhi::TextureHandle();
            for (usize i = 0; i < images_.Size(); ++i) {
                if (images_[i].id != cmd.texture.id) continue;
                texture = images_[i].source->Handle();
                if (!texture.IsValid() && !images_[i].logged_missing) {
                    images_[i].logged_missing = true;
                    RenderLog(LogVerbosity::Warn, "UiRenderer: image %u has no texture now; the commands that draw it are skipped", cmd.texture.id);
                }
            }
        }
        if (!fits || x1 <= x0 || y1 <= y0 || cmd.index_count == 0 || !texture.IsValid()) {
            ++stats_.dropped_commands;
            continue;
        }
        if (!group.IsValid() || !(texture == bound_texture)) {
            rhi::BindGroupEntry entries[2];
            entries[0].binding = 0;
            entries[0].texture = texture;
            entries[1].binding = 1;
            entries[1].sampler = sampler_.Handle();
            rhi::BindGroupDesc gd;
            gd.layout = layout_;
            gd.entries = rhi::Span<rhi::BindGroupEntry>(entries, 2);
            gd.transient = true;
            group = device_->CreateBindGroup(gd);
            bound_texture = texture;
            if (!group.IsValid()) {
                ++stats_.dropped_commands;
                continue;
            }
            list->SetBindGroup(0, group);
        }
        list->SetScissor(rhi::ScissorRect{static_cast<i32>(x0), static_cast<i32>(y0), static_cast<u32>(x1 - x0), static_cast<u32>(y1 - y0)});
        list->DrawIndexed(cmd.index_count, 1, cmd.first_index, 0, 0);
        ++stats_.draws;
    }
}

} // namespace engine_placeholder::renderer
