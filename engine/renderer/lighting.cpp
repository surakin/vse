#include "renderer/lighting.h"

#include <cmath>
#include <cstring>

#include "fullscreen.vert.spv.inc"
#include "lighting.frag.spv.inc"
#include "log/assert.h"
#include "renderer/renderer_log.h"
#include "tonemap.frag.spv.inc"

namespace engine_placeholder::renderer {

namespace {

constexpr f32 kPi = 3.14159265358979323846f;

f32 Saturate(f32 v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

f32 Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 Normalized(const Vec3& v) {
    const f32 len = std::sqrt(Dot(v, v));
    return Vec3(v.x / len, v.y / len, v.z / len);
}

} // namespace

Vec3 ShadeLight(const SurfacePoint& s, const Vec3& view, const Vec3& light, const Vec3& radiance) {
    const f32 nl = Saturate(Dot(s.normal, light));
    if (nl <= 0.0f) return Vec3(0, 0, 0);
    const f32 nv = std::fmax(Dot(s.normal, view), 1e-4f);
    const Vec3 vl = view + light;
    const Vec3 h = Dot(vl, vl) > 1e-10f ? Normalized(vl) : s.normal;
    const f32 nh = Saturate(Dot(s.normal, h));
    const f32 vh = Saturate(Dot(view, h));
    const f32 r = std::fmax(s.roughness, 0.045f);
    const f32 a = r * r;
    const f32 a2 = a * a;
    const f32 d_term = nh * nh * (a2 - 1.0f) + 1.0f;
    const f32 d = a2 / (kPi * d_term * d_term);
    const f32 vis = 0.5f / (nl * std::sqrt(nv * nv * (1.0f - a2) + a2) + nv * std::sqrt(nl * nl * (1.0f - a2) + a2));
    const f32 fresnel = std::pow(1.0f - vh, 5.0f);
    const f32 f0[3] = {0.04f + (s.albedo.x - 0.04f) * s.metallic, 0.04f + (s.albedo.y - 0.04f) * s.metallic, 0.04f + (s.albedo.z - 0.04f) * s.metallic};
    const f32 albedo[3] = {s.albedo.x, s.albedo.y, s.albedo.z};
    const f32 rad[3] = {radiance.x, radiance.y, radiance.z};
    f32 out[3];
    for (int c = 0; c < 3; ++c) {
        const f32 f = f0[c] + (1.0f - f0[c]) * fresnel;
        const f32 diffuse = albedo[c] * (1.0f - s.metallic);
        const f32 specular = kPi * d * vis * f;
        out[c] = (diffuse + specular) * rad[c] * nl;
    }
    return Vec3(out[0], out[1], out[2]);
}

Vec3 ShadeAmbient(const SurfacePoint& s, const Vec3& ambient) {
    const f32 k = (1.0f - s.metallic) * s.occlusion;
    return Vec3(s.albedo.x * (k * ambient.x + s.emissive_scale), s.albedo.y * (k * ambient.y + s.emissive_scale), s.albedo.z * (k * ambient.z + s.emissive_scale));
}

f32 TonemapAces(f32 x) { return Saturate((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f)); }

Vec3 ReconstructWorldPosition(u32 pixel_x, u32 pixel_y, f32 depth, u32 width, u32 height, const Mat4& inverse_view_projection) {
    const f32 nx = (static_cast<f32>(pixel_x) + 0.5f) / static_cast<f32>(width) * 2.0f - 1.0f;
    const f32 ny = (static_cast<f32>(pixel_y) + 0.5f) / static_cast<f32>(height) * 2.0f - 1.0f;
    const Vec4 c = inverse_view_projection * Vec4(nx, ny, depth, 1.0f);
    return Vec3(c.x / c.w, c.y / c.w, c.z / c.w);
}

// ---- LightingPass ----

LightingPass::LightingPass(rhi::Device* device, MaterialSystem* materials, FrameResources* frame) : device_(device), materials_(materials), frame_(frame) {
    ENGINE_ASSERT(device != nullptr && materials != nullptr && frame != nullptr, "LightingPass: device, materials or frame is null");
    rhi::ShaderDesc sd;
    sd.stage = rhi::ShaderStage::Vertex;
    sd.words = shaders::kFullscreenVertSpv;
    sd.word_count = shaders::kFullscreenVertSpvWordCount;
    vertex_ = device->CreateShader(sd);
    sd.stage = rhi::ShaderStage::Fragment;
    sd.words = shaders::kLightingFragSpv;
    sd.word_count = shaders::kLightingFragSpvWordCount;
    fragment_ = device->CreateShader(sd);
    rhi::BindingDesc bindings[5];
    for (u32 i = 0; i < 4; ++i) bindings[i] = {i, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment, 1};
    bindings[4] = {4, rhi::BindingType::StorageBuffer, rhi::ShaderStage::Fragment, 1};
    rhi::BindGroupLayoutDesc ld;
    ld.entries = rhi::Span<rhi::BindingDesc>(bindings, 5);
    layout_ = device->CreateBindGroupLayout(ld);
    if (!vertex_.IsValid() || !fragment_.IsValid() || !layout_.IsValid()) {
        RenderLog(LogVerbosity::Error, "LightingPass: creating the shaders or the bind group layout failed");
        return;
    }
    rhi::GraphicsPipelineDesc gp;
    gp.vertex_shader = vertex_;
    gp.fragment_shader = fragment_;
    gp.cull_mode = rhi::CullMode::None;
    gp.color_targets[0].format = kLightAccumFormat;
    gp.color_target_count = 1;
    gp.bind_group_layouts[0] = materials->FrameLayout();
    gp.bind_group_layouts[1] = layout_;
    gp.bind_group_layout_count = 2;
    gp.push_constant_bytes = sizeof(Push);
    gp.push_constant_stages = rhi::ShaderStage::Fragment;
    gp.debug_name = "lighting";
    pipeline_ = device->CreateGraphicsPipeline(gp);
    if (!pipeline_.IsValid()) RenderLog(LogVerbosity::Error, "LightingPass: creating the pipeline failed");
}

LightingPass::~LightingPass() {
    if (pipeline_.IsValid()) device_->DestroyPipeline(pipeline_);
    if (layout_.IsValid()) device_->DestroyBindGroupLayout(layout_);
    if (fragment_.IsValid()) device_->DestroyShader(fragment_);
    if (vertex_.IsValid()) device_->DestroyShader(vertex_);
}

LightSummary LightingPass::Prepare(const RenderScene& scene) {
    added_ = false;
    const LightSummary summary = frame_->WriteLights(scene);
    push_.ambient = Vec4(summary.ambient, 0.0f);
    push_.directional_count = summary.directional_count;
    lights_ = frame_->LightRegion();
    frame_group_ = materials_->FrameBindGroup(frame_->FrameRegion());
    return summary;
}

GraphResource LightingPass::AddToGraph(RenderGraph* graph, const GBufferTargets& targets, u32 width, u32 height) {
    ENGINE_ASSERT(graph != nullptr && width > 0 && height > 0, "LightingPass::AddToGraph: graph is null or the size is zero");
    ENGINE_ASSERT(!added_, "LightingPass::AddToGraph: called twice since the last Prepare; one pass object serves one view per frame");
    added_ = true;
    rhi::TextureDesc td;
    td.width = width;
    td.height = height;
    td.format = kLightAccumFormat;
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::CopySrc;
    td.debug_name = "light_accum";
    accum_ = graph->CreateTexture("light_accum", td);
    targets_ = targets;
    width_ = width;
    height_ = height;
    graph->AddPass("lighting", &LightingPass::Record, this)
        .Read(targets.albedo, GraphUsage::Sampled)
        .Read(targets.normal, GraphUsage::Sampled)
        .Read(targets.material, GraphUsage::Sampled)
        .Read(targets.depth, GraphUsage::Sampled)
        .Write(accum_, GraphUsage::ColorAttachment);
    return accum_;
}

void LightingPass::Record(const PassExecuteContext& context, void* user) {
    const LightingPass* self = static_cast<const LightingPass*>(user);
    rhi::CommandList* list = context.list;
    rhi::BindGroupHandle textures;
    if (self->pipeline_.IsValid() && self->frame_group_.IsValid()) {
        rhi::BindGroupEntry e[5];
        e[0].binding = 0;
        e[0].texture = context.graph->ResolvedTexture(self->targets_.albedo);
        e[1].binding = 1;
        e[1].texture = context.graph->ResolvedTexture(self->targets_.normal);
        e[2].binding = 2;
        e[2].texture = context.graph->ResolvedTexture(self->targets_.material);
        e[3].binding = 3;
        e[3].texture = context.graph->ResolvedTexture(self->targets_.depth);
        e[4].binding = 4;
        e[4].buffer = self->lights_.buffer;
        e[4].offset = self->lights_.offset;
        e[4].size = self->lights_.size;
        rhi::BindGroupDesc gd;
        gd.layout = self->layout_;
        gd.entries = rhi::Span<rhi::BindGroupEntry>(e, 5);
        gd.transient = true;
        textures = self->device_->CreateBindGroup(gd);
    }
    rhi::RenderingDesc rd;
    rd.color_count = 1;
    rd.color[0].texture = context.graph->ResolvedTexture(self->accum_);
    rd.color[0].load = rhi::LoadOp::Clear;
    rd.color[0].clear_color[0] = 0.0f;
    rd.color[0].clear_color[1] = 0.0f;
    rd.color[0].clear_color[2] = 0.0f;
    rd.color[0].clear_color[3] = 0.0f;
    list->BeginRendering(rd);
    if (textures.IsValid()) {
        list->SetPipeline(self->pipeline_);
        list->SetViewport(rhi::Viewport{0.0f, 0.0f, static_cast<f32>(self->width_), static_cast<f32>(self->height_), 0.0f, 1.0f});
        list->SetScissor(rhi::ScissorRect{0, 0, self->width_, self->height_});
        list->SetBindGroup(kFrameSet, self->frame_group_);
        list->SetBindGroup(1, textures);
        list->SetPushConstants(0, &self->push_, sizeof(Push));
        list->Draw(3, 1, 0, 0);
    } else {
        RenderLog(LogVerbosity::Error, "LightingPass: the pipeline or a bind group is missing; the accumulation target stays black");
    }
    list->EndRendering();
}

// ---- TonemapPass ----

TonemapPass::TonemapPass(rhi::Device* device) : device_(device) {
    ENGINE_ASSERT(device != nullptr, "TonemapPass: device is null");
    rhi::ShaderDesc sd;
    sd.stage = rhi::ShaderStage::Vertex;
    sd.words = shaders::kFullscreenVertSpv;
    sd.word_count = shaders::kFullscreenVertSpvWordCount;
    vertex_ = device->CreateShader(sd);
    sd.stage = rhi::ShaderStage::Fragment;
    sd.words = shaders::kTonemapFragSpv;
    sd.word_count = shaders::kTonemapFragSpvWordCount;
    fragment_ = device->CreateShader(sd);
    rhi::BindingDesc binding = {0, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment, 1};
    rhi::BindGroupLayoutDesc ld;
    ld.entries = rhi::Span<rhi::BindingDesc>(&binding, 1);
    layout_ = device->CreateBindGroupLayout(ld);
}

TonemapPass::~TonemapPass() {
    if (pipeline_.IsValid()) device_->DestroyPipeline(pipeline_);
    if (layout_.IsValid()) device_->DestroyBindGroupLayout(layout_);
    if (fragment_.IsValid()) device_->DestroyShader(fragment_);
    if (vertex_.IsValid()) device_->DestroyShader(vertex_);
}

bool TonemapPass::Valid() const { return vertex_.IsValid() && fragment_.IsValid() && layout_.IsValid(); }

void TonemapPass::AddToGraph(RenderGraph* graph, GraphResource hdr, GraphResource output, rhi::Format output_format, f32 exposure) {
    ENGINE_ASSERT(graph != nullptr && output_format != rhi::Format::Undefined, "TonemapPass::AddToGraph: graph is null or the output format is undefined");
    hdr_ = hdr;
    output_ = output;
    push_.exposure = exposure;
    output_format_ = output_format;
    // The pipeline is built here, on the frame's thread, not inside the recording callback.
    if (pipeline_format_ != output_format_) {
        if (pipeline_.IsValid()) device_->DestroyPipeline(pipeline_);
        rhi::GraphicsPipelineDesc gp;
        gp.vertex_shader = vertex_;
        gp.fragment_shader = fragment_;
        gp.cull_mode = rhi::CullMode::None;
        gp.color_targets[0].format = output_format_;
        gp.color_target_count = 1;
        gp.bind_group_layouts[0] = layout_;
        gp.bind_group_layout_count = 1;
        gp.push_constant_bytes = sizeof(Push);
        gp.push_constant_stages = rhi::ShaderStage::Fragment;
        gp.debug_name = "tonemap";
        pipeline_ = device_->CreateGraphicsPipeline(gp);
        pipeline_format_ = pipeline_.IsValid() ? output_format_ : rhi::Format::Undefined;
    }
    graph->AddPass("tonemap", &TonemapPass::Record, this).Read(hdr, GraphUsage::Sampled).Write(output, GraphUsage::ColorAttachment);
}

void TonemapPass::Record(const PassExecuteContext& context, void* user) {
    TonemapPass* self = static_cast<TonemapPass*>(user);
    rhi::Device* device = self->device_;
    if (!self->pipeline_.IsValid()) {
        RenderLog(LogVerbosity::Error, "TonemapPass: the pipeline for the output format could not be created; nothing is drawn");
        return;
    }
    const rhi::TextureDesc& hd = context.graph->TextureDescOf(self->hdr_);
    rhi::BindGroupEntry e;
    e.binding = 0;
    e.texture = context.graph->ResolvedTexture(self->hdr_);
    rhi::BindGroupDesc gd;
    gd.layout = self->layout_;
    gd.entries = rhi::Span<rhi::BindGroupEntry>(&e, 1);
    gd.transient = true;
    const rhi::BindGroupHandle group = device->CreateBindGroup(gd);
    rhi::RenderingDesc rd;
    rd.color_count = 1;
    rd.color[0].texture = context.graph->IsImport(self->output_) ? context.graph->ImportedTexture(self->output_) : context.graph->ResolvedTexture(self->output_);
    rd.color[0].load = rhi::LoadOp::Clear;
    context.list->BeginRendering(rd);
    context.list->SetPipeline(self->pipeline_);
    context.list->SetViewport(rhi::Viewport{0.0f, 0.0f, static_cast<f32>(hd.width), static_cast<f32>(hd.height), 0.0f, 1.0f});
    context.list->SetScissor(rhi::ScissorRect{0, 0, hd.width, hd.height});
    context.list->SetBindGroup(0, group);
    context.list->SetPushConstants(0, &self->push_, sizeof(Push));
    context.list->Draw(3, 1, 0, 0);
    context.list->EndRendering();
}

} // namespace engine_placeholder::renderer
