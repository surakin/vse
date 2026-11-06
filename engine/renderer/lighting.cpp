#include "renderer/lighting.h"

#include <cmath>
#include <cstring>

#include "fullscreen.vert.spv.inc"
#include "lighting.frag.spv.inc"
#include "point_light.frag.spv.inc"
#include "point_light.vert.spv.inc"
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

f32 PointLightAttenuation(f32 distance, f32 radius) {
    const f32 r = distance / radius;
    const f32 window = Saturate(1.0f - r * r * r * r);
    return window * window / (distance * distance + 1.0f);
}

f32 LightVolumeScale() {
    return 1.0f / (std::cos(kPi / static_cast<f32>(kLightVolumeSegments)) * std::cos(kPi / static_cast<f32>(kLightVolumeRings)));
}

Vec3 LightVolumeVertex(u32 index) {
    ENGINE_ASSERT(index < kLightVolumeVertexCount, "LightVolumeVertex: index out of range");
    // Two triangles per quad, counter-clockwise seen from outside: (slice, ring) = (0,0) (1,0) (0,1) and (1,0) (1,1) (0,1), the order of MakeSphereMesh.
    static const u32 kCorner[6][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 0}, {1, 1}, {0, 1}};
    const u32 quad = index / 6;
    const u32 slice = quad % kLightVolumeSegments + kCorner[index % 6][0];
    const u32 ring = quad / kLightVolumeSegments + kCorner[index % 6][1];
    const f32 theta = kPi * static_cast<f32>(ring) / static_cast<f32>(kLightVolumeRings);
    const f32 phi = 2.0f * kPi * static_cast<f32>(slice) / static_cast<f32>(kLightVolumeSegments);
    const f32 scale = LightVolumeScale();
    return Vec3(std::sin(theta) * std::cos(phi) * scale, std::cos(theta) * scale, std::sin(theta) * std::sin(phi) * scale);
}

bool LightScreenRect(const FrameData& view, const Vec3& center, f32 radius, u32 width, u32 height, rhi::ScissorRect* out) {
    ENGINE_ASSERT(out != nullptr && width > 0 && height > 0 && radius > 0.0f, "LightScreenRect: bad arguments");
    f32 min_x = 1e30f, min_y = 1e30f, max_x = -1e30f, max_y = -1e30f;
    bool all_in_front = true;
    for (int corner = 0; corner < 8; ++corner) {
        const Vec4 c = view.view_projection * Vec4(center.x + ((corner & 1) ? radius : -radius), center.y + ((corner & 2) ? radius : -radius),
                                                   center.z + ((corner & 4) ? radius : -radius), 1.0f);
        if (!(c.w > 1e-4f)) {
            all_in_front = false;
            break;
        }
        const f32 nx = c.x / c.w;
        const f32 ny = c.y / c.w;
        min_x = std::fmin(min_x, nx);
        max_x = std::fmax(max_x, nx);
        min_y = std::fmin(min_y, ny);
        max_y = std::fmax(max_y, ny);
    }
    if (!all_in_front) {
        *out = rhi::ScissorRect{0, 0, width, height};
        return true;
    }
    // NDC y already carries the projection's flip, so row 0 is the top of the framebuffer, as in the lighting shader.
    const f32 fw = static_cast<f32>(width);
    const f32 fh = static_cast<f32>(height);
    const f32 x0 = std::fmax(std::floor((min_x * 0.5f + 0.5f) * fw), 0.0f);
    const f32 x1 = std::fmin(std::ceil((max_x * 0.5f + 0.5f) * fw), fw);
    const f32 y0 = std::fmax(std::floor((min_y * 0.5f + 0.5f) * fh), 0.0f);
    const f32 y1 = std::fmin(std::ceil((max_y * 0.5f + 0.5f) * fh), fh);
    if (!(x1 > x0) || !(y1 > y0)) {
        return false;
    }
    *out = rhi::ScissorRect{static_cast<i32>(x0), static_cast<i32>(y0), static_cast<u32>(x1 - x0), static_cast<u32>(y1 - y0)};
    return true;
}

// ---- LightingPass ----

LightingPass::LightingPass(rhi::Device* device, MaterialSystem* materials, FrameResources* frame) : device_(device), materials_(materials), frame_(frame) {
    ENGINE_ASSERT(device != nullptr && materials != nullptr && frame != nullptr, "LightingPass: device, materials or frame is null");
    Build();
}

void LightingPass::Build() {
    rhi::Device* device = device_;
    MaterialSystem* materials = materials_;
    rhi::ShaderDesc sd;
    sd.stage = rhi::ShaderStage::Vertex;
    sd.words = shaders::kFullscreenVertSpv;
    sd.word_count = shaders::kFullscreenVertSpvWordCount;
    vertex_ = device->CreateShader(sd);
    sd.stage = rhi::ShaderStage::Fragment;
    sd.words = shaders::kLightingFragSpv;
    sd.word_count = shaders::kLightingFragSpvWordCount;
    fragment_ = device->CreateShader(sd);
    sd.stage = rhi::ShaderStage::Vertex;
    sd.words = shaders::kPointLightVertSpv;
    sd.word_count = shaders::kPointLightVertSpvWordCount;
    volume_vertex_ = device->CreateShader(sd);
    sd.stage = rhi::ShaderStage::Fragment;
    sd.words = shaders::kPointLightFragSpv;
    sd.word_count = shaders::kPointLightFragSpvWordCount;
    volume_fragment_ = device->CreateShader(sd);
    rhi::BindingDesc bindings[5];
    for (u32 i = 0; i < 4; ++i) bindings[i] = {i, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment, 1};
    bindings[4] = {4, rhi::BindingType::StorageBuffer, rhi::ShaderStage::AllGraphics, 1}; // the light volume's vertex shader reads it too
    rhi::BindGroupLayoutDesc ld;
    ld.entries = rhi::Span<rhi::BindingDesc>(bindings, 5);
    layout_ = device->CreateBindGroupLayout(ld);
    if (!vertex_.IsValid() || !fragment_.IsValid() || !volume_vertex_.IsValid() || !volume_fragment_.IsValid() || !layout_.IsValid()) {
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

    // The light volume: no vertex buffer, additive blending, front faces culled, no depth attachment.
    rhi::GraphicsPipelineDesc vp;
    vp.vertex_shader = volume_vertex_;
    vp.fragment_shader = volume_fragment_;
    vp.cull_mode = rhi::CullMode::Front;
    vp.color_targets[0].format = kLightAccumFormat;
    vp.color_targets[0].blend.enable = true;
    vp.color_targets[0].blend.src_color = rhi::BlendFactor::One;
    vp.color_targets[0].blend.dst_color = rhi::BlendFactor::One;
    vp.color_targets[0].blend.src_alpha = rhi::BlendFactor::One;
    vp.color_targets[0].blend.dst_alpha = rhi::BlendFactor::One;
    vp.color_target_count = 1;
    vp.bind_group_layouts[0] = materials->FrameLayout();
    vp.bind_group_layouts[1] = layout_;
    vp.bind_group_layout_count = 2;
    vp.debug_name = "point light volume";
    volume_pipeline_ = device->CreateGraphicsPipeline(vp);
    if (!volume_pipeline_.IsValid()) RenderLog(LogVerbosity::Error, "LightingPass: creating the light volume pipeline failed");
}

LightingPass::~LightingPass() {
    registration_.Reset();
    Release();
}

void LightingPass::Release() {
    if (device_ == nullptr) return;
    if (volume_pipeline_.IsValid()) device_->DestroyPipeline(volume_pipeline_);
    if (volume_fragment_.IsValid()) device_->DestroyShader(volume_fragment_);
    if (volume_vertex_.IsValid()) device_->DestroyShader(volume_vertex_);
    if (pipeline_.IsValid()) device_->DestroyPipeline(pipeline_);
    if (layout_.IsValid()) device_->DestroyBindGroupLayout(layout_);
    if (fragment_.IsValid()) device_->DestroyShader(fragment_);
    if (vertex_.IsValid()) device_->DestroyShader(vertex_);
    volume_pipeline_ = {};
    volume_fragment_ = {};
    volume_vertex_ = {};
    pipeline_ = {};
    layout_ = {};
    fragment_ = {};
    vertex_ = {};
    frame_group_ = {}; // a handle from the old device; Prepare fetches it again
}

bool LightingPass::Recreate(rhi::Device* device) {
    device_ = device;
    Build();
    if (!pipeline_.IsValid() || !volume_pipeline_.IsValid()) {
        RenderLog(LogVerbosity::Error, "LightingPass: recreating the shaders or pipelines failed");
        Release();
        return false;
    }
    return true;
}

LightSummary LightingPass::Prepare(const RenderScene& scene, const FrameData& view) {
    added_ = false;
    const LightSummary summary = frame_->WriteLights(scene, view);
    push_.ambient = Vec4(summary.ambient, 0.0f);
    push_.directional_count = summary.directional_count;
    // One draw per visible point light, in buffer order (nearest first), scissored to its screen rectangle.
    point_draws_.Clear();
    const u32 width = static_cast<u32>(view.viewport.x);
    const u32 height = static_cast<u32>(view.viewport.y);
    ENGINE_ASSERT(width > 0 && height > 0, "LightingPass::Prepare: the viewport size is below 1 pixel");
    prepared_width_ = width;
    prepared_height_ = height;
    const LightData* written = frame_->WrittenLights();
    for (u32 i = 0; i < summary.point_count; ++i) {
        const u32 index = summary.directional_count + i;
        PointDraw draw;
        draw.light_index = index;
        const Vec4& pr = written[index].position_radius;
        if (LightScreenRect(view, Vec3(pr.x, pr.y, pr.z), pr.w, width, height, &draw.rect)) {
            point_draws_.Push(draw);
        }
    }
    lights_ = frame_->LightRegion();
    frame_group_ = materials_->FrameBindGroup(frame_->FrameRegion());
    return summary;
}

GraphResource LightingPass::AddToGraph(RenderGraph* graph, const GBufferTargets& targets, u32 width, u32 height) {
    ENGINE_ASSERT(graph != nullptr && width > 0 && height > 0, "LightingPass::AddToGraph: graph is null or the size is zero");
    ENGINE_ASSERT(!added_, "LightingPass::AddToGraph: called twice since the last Prepare; one pass object serves one view per frame");
    ENGINE_ASSERT(width == prepared_width_ && height == prepared_height_, "LightingPass::AddToGraph: the size differs from the viewport given to Prepare");
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
    LightingPass* self = static_cast<LightingPass*>(user);
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
        // Point lights: one pipeline, the same bind groups, one draw per light picked by the instance index. The groups are
        // bound again because the pipeline layout differs in its push constant range.
        u32 recorded = 0;
        if (self->point_draws_.Size() > 0 && self->volume_pipeline_.IsValid()) {
            list->SetPipeline(self->volume_pipeline_);
            list->SetBindGroup(kFrameSet, self->frame_group_);
            list->SetBindGroup(1, textures);
            for (usize i = 0; i < self->point_draws_.Size(); ++i) {
                list->SetScissor(self->point_draws_[i].rect);
                list->Draw(kLightVolumeVertexCount, 1, 0, self->point_draws_[i].light_index);
                ++recorded;
            }
        } else if (self->point_draws_.Size() > 0) {
            RenderLog(LogVerbosity::Error, "LightingPass: the light volume pipeline is missing; point lights are not drawn");
        }
        self->point_draws_recorded_.store(recorded, std::memory_order_relaxed);
    } else {
        RenderLog(LogVerbosity::Error, "LightingPass: the pipeline or a bind group is missing; the accumulation target stays black");
    }
    list->EndRendering();
}

// ---- TonemapPass ----

TonemapPass::TonemapPass(rhi::Device* device) : device_(device) {
    ENGINE_ASSERT(device != nullptr, "TonemapPass: device is null");
    Build();
}

void TonemapPass::Build() {
    rhi::Device* device = device_;
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
    registration_.Reset();
    Release();
}

void TonemapPass::Release() {
    if (device_ == nullptr) return;
    if (pipeline_.IsValid()) device_->DestroyPipeline(pipeline_);
    if (layout_.IsValid()) device_->DestroyBindGroupLayout(layout_);
    if (fragment_.IsValid()) device_->DestroyShader(fragment_);
    if (vertex_.IsValid()) device_->DestroyShader(vertex_);
    pipeline_ = {};
    layout_ = {};
    fragment_ = {};
    vertex_ = {};
    pipeline_format_ = rhi::Format::Undefined; // made again by the next AddToGraph, for whatever the output format is then
}

bool TonemapPass::Recreate(rhi::Device* device) {
    device_ = device;
    Build();
    if (!Valid()) {
        RenderLog(LogVerbosity::Error, "TonemapPass: recreating the shaders or layout failed");
        Release();
        return false;
    }
    return true;
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
