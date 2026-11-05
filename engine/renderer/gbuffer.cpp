#include "renderer/gbuffer.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "fullscreen.vert.spv.inc"
#include "gbuffer.frag.perm.inc"
#include "gbuffer.vert.perm.inc"
#include "gbuffer_debug.frag.spv.inc"
#include "log/assert.h"
#include "renderer/renderer_log.h"

namespace engine_placeholder::renderer {

namespace {

const char* FormatLabel(rhi::Format format) {
    switch (format) {
    case rhi::Format::RGBA8_SRGB:
        return "RGBA8_SRGB";
    case rhi::Format::RG16_Float:
        return "RG16_Float";
    case rhi::Format::RGBA8_UNorm:
        return "RGBA8_UNorm";
    case rhi::Format::D32_Float:
        return "D32_Float";
    default:
        return "unknown";
    }
}

f32 SignNotZero(f32 v) { return v >= 0.0f ? 1.0f : -1.0f; }

// The location of the per-vertex color attribute in ColorVertexLayout.
constexpr u32 kColorLocation = 4;

bool HasColor(const VertexLayoutData& layout) {
    for (u32 i = 0; i < layout.attribute_count; ++i) {
        if (layout.attributes[i].location == kColorLocation && layout.attributes[i].format == rhi::VertexFormat::UNorm8x4) return true;
    }
    return false;
}

// One visible object on its way into the draw list.
struct Entry {
    u64 key = 0;
    u32 slot = 0;
    rhi::PipelineHandle pipeline;
    rhi::BindGroupHandle material_group;
    const GpuMesh* mesh = nullptr;
};

int CompareEntries(const void* a, const void* b) {
    const Entry* x = static_cast<const Entry*>(a);
    const Entry* y = static_cast<const Entry*>(b);
    if (x->key != y->key) return x->key < y->key ? -1 : 1;
    if (x->slot != y->slot) return x->slot < y->slot ? -1 : 1;
    return 0;
}

} // namespace

bool CheckGBufferFormats(const rhi::Device* device, char* error, usize error_capacity) {
    struct Need {
        rhi::Format format;
        rhi::TextureUsage usage;
        const char* usage_name;
    };
    const Need needs[] = {
        {kGBufferAlbedoFormat, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled, "color attachment and sampled"},
        {kGBufferNormalFormat, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled, "color attachment and sampled"},
        {kGBufferMaterialFormat, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled, "color attachment and sampled"},
        {kGBufferDepthFormat, rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled, "depth attachment and sampled"},
    };
    for (const Need& need : needs) {
        if (!device->FormatSupport(need.format, need.usage)) {
            if (error != nullptr && error_capacity > 0) {
                std::snprintf(error, error_capacity, "G-buffer: the device does not support format %s as %s", FormatLabel(need.format), need.usage_name);
            }
            return false;
        }
    }
    if (error != nullptr && error_capacity > 0) error[0] = '\0';
    return true;
}

PipelineTarget GBufferPipelineTarget() {
    PipelineTarget t;
    t.color_formats[0] = kGBufferAlbedoFormat;
    t.color_formats[1] = kGBufferNormalFormat;
    t.color_formats[2] = kGBufferMaterialFormat;
    t.color_count = 3;
    t.depth_format = kGBufferDepthFormat;
    return t;
}

Vec2 OctEncode(const Vec3& n) {
    const f32 sum = std::fabs(n.x) + std::fabs(n.y) + std::fabs(n.z);
    f32 px = n.x / sum;
    f32 py = n.y / sum;
    if (n.z < 0.0f) {
        const f32 nx = (1.0f - std::fabs(py)) * SignNotZero(px);
        const f32 ny = (1.0f - std::fabs(px)) * SignNotZero(py);
        px = nx;
        py = ny;
    }
    return Vec2(px, py);
}

Vec3 OctDecode(const Vec2& f) {
    f32 x = f.x;
    f32 y = f.y;
    const f32 z = 1.0f - std::fabs(x) - std::fabs(y);
    if (z < 0.0f) {
        const f32 nx = (1.0f - std::fabs(y)) * SignNotZero(x);
        const f32 ny = (1.0f - std::fabs(x)) * SignNotZero(y);
        x = nx;
        y = ny;
    }
    const f32 len = std::sqrt(x * x + y * y + z * z);
    return Vec3(x / len, y / len, z / len);
}

f32 SrgbToLinear(f32 c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

f32 LinearizeDepth(f32 depth, f32 near_plane, f32 far_plane) { return near_plane * far_plane / (far_plane - depth * (far_plane - near_plane)); }

void FitCameraDepthRange(const RenderScene& scene, Camera* camera, f32 min_near) {
    ENGINE_ASSERT(camera != nullptr && min_near > 0.0f, "FitCameraDepthRange: camera is null or min_near is not positive");
    const Vec3 forward = camera->Forward();
    f32 nearest = 0.0f;
    f32 farthest = 0.0f;
    bool any = false;
    for (u32 slot = 0; slot < scene.SlotCount(); ++slot) {
        if (!scene.SlotAlive(slot)) continue;
        const BoundingSphere& s = scene.ObjectAt(slot).bounds.sphere;
        const Vec3 d = s.center - camera->position;
        const f32 along = d.x * forward.x + d.y * forward.y + d.z * forward.z;
        const f32 lo = along - s.radius;
        const f32 hi = along + s.radius;
        if (!any || lo < nearest) nearest = lo;
        if (!any || hi > farthest) farthest = hi;
        any = true;
    }
    if (!any) return;
    camera->near_plane = nearest > min_near ? nearest : min_near;
    camera->far_plane = farthest > camera->near_plane * 2.0f ? farthest : camera->near_plane * 2.0f;
}

GBufferTemplates CreateGBufferTemplates(MaterialSystem* materials) {
    ENGINE_ASSERT(materials != nullptr, "CreateGBufferTemplates: materials is null");
    GBufferTemplates out;
    const TextureDefault defaults[2] = {{"albedo_tex", DefaultTexture::White}, {"normal_tex", DefaultTexture::FlatNormal}};
    for (int colored = 0; colored < 2; ++colored) {
        PermutationKey vkey;
        ReflectionError error;
        const char* names[1] = {"VERTEX_COLOR"};
        if (!MakePermutationKey(shaders::kGbufferVertPermutations, colored ? names : nullptr, colored ? 1u : 0u, &vkey, &error)) {
            RenderLog(LogVerbosity::Error, "G-buffer: vertex shader permutation: %s", error.message);
            if (out.plain.IsValid()) materials->DestroyTemplate(out.plain);
            return GBufferTemplates{};
        }
        const PermutationModule* vertex = FindPermutation(shaders::kGbufferVertPermutations, vkey, &error);
        if (vertex == nullptr) {
            RenderLog(LogVerbosity::Error, "G-buffer: vertex shader permutation: %s", error.message);
            if (out.plain.IsValid()) materials->DestroyTemplate(out.plain);
            return GBufferTemplates{};
        }
        MaterialTemplateDesc d;
        d.name = colored ? "gbuffer_color" : "gbuffer";
        d.vertex = *vertex;
        d.fragment_permutations = &shaders::kGbufferFragPermutations;
        d.state.cull = rhi::CullMode::Back;
        d.state.depth_test = true;
        d.state.depth_write = true;
        d.state.depth_compare = rhi::CompareOp::Less;
        d.texture_defaults = defaults;
        d.texture_default_count = 2;
        const TemplateId id = materials->CreateTemplate(d);
        if (!id.IsValid()) {
            if (out.plain.IsValid()) materials->DestroyTemplate(out.plain);
            return GBufferTemplates{};
        }
        (colored ? out.colored : out.plain) = id;
    }
    return out;
}

MaterialId CreateGBufferMaterial(MaterialSystem* materials, const GBufferTemplates& templates, const GBufferOptions& options, const char* name) {
    ENGINE_ASSERT(materials != nullptr, "CreateGBufferMaterial: materials is null");
    const TemplateId tmpl = options.vertex_color ? templates.colored : templates.plain;
    if (!tmpl.IsValid()) {
        RenderLog(LogVerbosity::Error, "G-buffer: the %s template is invalid", options.vertex_color ? "vertex color" : "plain");
        return MaterialId{};
    }
    const char* names[3];
    u32 count = 0;
    if (options.normal_map) names[count++] = "NORMAL_MAP";
    if (options.alpha_test) names[count++] = "ALPHA_TEST";
    if (options.vertex_color) names[count++] = "VERTEX_COLOR";
    PermutationKey key;
    ReflectionError error;
    if (!MakePermutationKey(shaders::kGbufferFragPermutations, names, count, &key, &error)) {
        RenderLog(LogVerbosity::Error, "G-buffer: fragment shader permutation: %s", error.message);
        return MaterialId{};
    }
    const MaterialId id = materials->CreateMaterial(tmpl, key, name != nullptr ? name : "gbuffer");
    if (!id.IsValid()) return MaterialId{};
    materials->SetVec4(id, "base_color", Vec4(1, 1, 1, 1));
    materials->SetFloat(id, "roughness", 0.5f);
    materials->SetFloat(id, "metallic", 0.0f);
    materials->SetFloat(id, "emissive_scale", 0.0f);
    materials->SetFloat(id, "alpha_cutoff", 0.5f);
    materials->SetFloat(id, "occlusion", 1.0f);
    materials->SetUInt(id, "flags", 0);
    return id;
}

// ---- GBufferPass ----

GBufferPass::GBufferPass(rhi::Device* device, MaterialSystem* materials, FrameResources* frame) : device_(device), materials_(materials), frame_(frame) {
    ENGINE_ASSERT(device != nullptr && materials != nullptr && frame != nullptr, "GBufferPass: device, materials or frame is null");
    const u32 cap = frame->MaxObjects();
    draws_ = new (std::nothrow) Draw[cap];
    slots_ = new (std::nothrow) u32[cap];
    sort_scratch_ = new (std::nothrow) Entry[cap];
    if (draws_ == nullptr || slots_ == nullptr || sort_scratch_ == nullptr) {
        RenderLog(LogVerbosity::Error, "GBufferPass: out of memory for %u objects", cap);
        delete[] draws_;
        delete[] slots_;
        delete[] static_cast<Entry*>(sort_scratch_);
        draws_ = nullptr;
        slots_ = nullptr;
        sort_scratch_ = nullptr;
    }
}

GBufferPass::~GBufferPass() {
    delete[] draws_;
    delete[] slots_;
    delete[] cull_slots_;
    delete[] static_cast<Entry*>(sort_scratch_);
}

void GBufferPass::Reject(const char* fmt, ...) {
    ++stats_.rejected;
    if (error_[0] != '\0') return; // the first message is kept; the rest are counted
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(error_, sizeof(error_), fmt, args);
    va_end(args);
}

bool GBufferPass::Prepare(const RenderScene& scene, const Frustum& frustum) {
    stats_ = GBufferStats{};
    error_[0] = '\0';
    draw_count_ = 0;
    added_ = false;
    if (draws_ == nullptr) {
        std::snprintf(error_, sizeof(error_), "G-buffer: the pass could not allocate its draw list");
        return false;
    }
    const u32 cap = frame_->MaxObjects();

    // The per-frame groups are cached by the material system; creating them is rare.
    frame_group_ = materials_->FrameBindGroup(frame_->FrameRegion());
    object_group_ = materials_->ObjectBindGroup(frame_->ObjectRegion());
    if (!frame_group_.IsValid() || !object_group_.IsValid()) {
        std::snprintf(error_, sizeof(error_), "G-buffer: the frame or object bind group could not be created");
        return false;
    }

    // Cull into a scratch array sized for the scene; the pass's own arrays hold at most FrameResources::MaxObjects().
    const u32 scene_slots = scene.SlotCount();
    u32* visible_slots = slots_;
    if (scene_slots > cap) {
        if (cull_capacity_ < scene_slots) {
            delete[] cull_slots_;
            cull_slots_ = new (std::nothrow) u32[scene_slots];
            cull_capacity_ = cull_slots_ != nullptr ? scene_slots : 0;
        }
        if (cull_slots_ == nullptr) {
            std::snprintf(error_, sizeof(error_), "G-buffer: out of memory for the cull list");
            return false;
        }
        visible_slots = cull_slots_;
    }
    const u32 visible = scene.Cull(frustum, visible_slots, scene_slots);
    stats_.visible = visible;

    Entry* entries = static_cast<Entry*>(sort_scratch_);
    u32 entry_count = 0;
    const PipelineTarget target = GBufferPipelineTarget();
    for (u32 i = 0; i < visible; ++i) {
        const u32 slot = visible_slots[i];
        const RenderObject& object = scene.ObjectAt(slot);
        if (entry_count == cap) {
            Reject("G-buffer: more visible objects (%u) than the frame resources hold (%u)", visible, cap);
            continue;
        }
        const GpuMesh* mesh = object.mesh;
        if (mesh == nullptr || !mesh->Valid()) {
            Reject("G-buffer: the object in slot %u has no mesh", slot);
            continue;
        }
        if (!mesh->Ready()) {
            Reject("G-buffer: the mesh of the object in slot %u is not prepared: record GpuMesh::RecordPrepare and call MarkReady after the submit", slot);
            continue;
        }
        const MaterialId material = materials_->MaterialAtIndex(object.material);
        if (!material.IsValid()) {
            Reject("G-buffer: the object in slot %u uses material %u, which does not exist", slot, object.material);
            continue;
        }
        const bool mesh_color = HasColor(mesh->Layout());
        const bool material_color = materials_->ReadsVertexInput(material, kColorLocation);
        if (mesh_color != material_color) {
            if (mesh_color) {
                Reject("G-buffer: the object in slot %u has a mesh with vertex colors but material %u does not use them (create the material with GBufferOptions::vertex_color)", slot, object.material);
            } else {
                Reject("G-buffer: the object in slot %u has a mesh without vertex colors but material %u expects them", slot, object.material);
            }
            continue;
        }
        const PipelineLookup pipeline = materials_->GetPipeline(material, target, mesh->Layout(), mesh->Topology(), mesh->HasPrimitiveRestart());
        if (!pipeline.pipeline.IsValid()) {
            Reject("G-buffer: no pipeline could be built for the object in slot %u with material %u", slot, object.material);
            continue;
        }
        Entry& e = entries[entry_count++];
        e.key = MakeDrawSortKey(pipeline.index, material.index, mesh->SortId());
        e.slot = slot;
        e.pipeline = pipeline.pipeline;
        e.material_group = materials_->MaterialBindGroup(material);
        e.mesh = mesh;
    }

    std::qsort(entries, entry_count, sizeof(Entry), CompareEntries);
    for (u32 i = 0; i < entry_count; ++i) slots_[i] = entries[i].slot;
    frame_->WriteObjects(scene, slots_, entry_count);

    // Runs of the same material and mesh become one instanced draw; the object data is in the same order, so the
    // instances of a run are consecutive entries.
    rhi::PipelineHandle last_pipeline;
    rhi::BindGroupHandle last_group;
    for (u32 i = 0; i < entry_count; ++i) {
        const Entry& e = entries[i];
        if (draw_count_ > 0) {
            Draw& prev = draws_[draw_count_ - 1];
            if (prev.mesh == e.mesh && prev.material_group == e.material_group && prev.pipeline == e.pipeline) {
                ++prev.instance_count;
                continue;
            }
        }
        Draw& d = draws_[draw_count_++];
        d.pipeline = e.pipeline;
        d.material_group = e.material_group;
        d.mesh = e.mesh;
        d.first_instance = i;
        d.instance_count = 1;
        if (!(d.pipeline == last_pipeline)) ++stats_.pipeline_binds;
        if (!(d.material_group == last_group)) ++stats_.material_binds;
        last_pipeline = d.pipeline;
        last_group = d.material_group;
    }
    stats_.drawn_objects = entry_count;
    stats_.draws = draw_count_;
    return stats_.rejected == 0;
}

GBufferTargets GBufferPass::AddToGraph(RenderGraph* graph, u32 width, u32 height) {
    ENGINE_ASSERT(graph != nullptr && width > 0 && height > 0, "GBufferPass::AddToGraph: graph is null or the size is zero");
    ENGINE_ASSERT(!added_, "GBufferPass::AddToGraph: called twice since the last Prepare; one pass object serves one view per frame");
    added_ = true;
    rhi::TextureDesc td;
    td.width = width;
    td.height = height;
    td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | extra_usage_;
    td.format = kGBufferAlbedoFormat;
    td.debug_name = "gbuffer_albedo";
    targets_.albedo = graph->CreateTexture("gbuffer_albedo", td);
    td.format = kGBufferNormalFormat;
    td.debug_name = "gbuffer_normal";
    targets_.normal = graph->CreateTexture("gbuffer_normal", td);
    td.format = kGBufferMaterialFormat;
    td.debug_name = "gbuffer_material";
    targets_.material = graph->CreateTexture("gbuffer_material", td);
    td.format = kGBufferDepthFormat;
    td.usage = rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled | extra_usage_;
    td.debug_name = "gbuffer_depth";
    targets_.depth = graph->CreateTexture("gbuffer_depth", td);
    width_ = width;
    height_ = height;
    graph->AddPass("gbuffer", &GBufferPass::Record, this)
        .Write(targets_.albedo, GraphUsage::ColorAttachment)
        .Write(targets_.normal, GraphUsage::ColorAttachment)
        .Write(targets_.material, GraphUsage::ColorAttachment)
        .Write(targets_.depth, GraphUsage::DepthAttachment);
    return targets_;
}

void GBufferPass::Record(const PassExecuteContext& context, void* user) {
    const GBufferPass* self = static_cast<const GBufferPass*>(user);
    rhi::CommandList* list = context.list;
    rhi::RenderingDesc rd;
    rd.color_count = 3;
    rd.color[0].texture = context.graph->ResolvedTexture(self->targets_.albedo);
    rd.color[1].texture = context.graph->ResolvedTexture(self->targets_.normal);
    rd.color[2].texture = context.graph->ResolvedTexture(self->targets_.material);
    for (u32 i = 0; i < 3; ++i) {
        rd.color[i].load = rhi::LoadOp::Clear;
        rd.color[i].store = rhi::StoreOp::Store;
        rd.color[i].clear_color[0] = 0.0f;
        rd.color[i].clear_color[1] = 0.0f;
        rd.color[i].clear_color[2] = 0.0f;
        rd.color[i].clear_color[3] = 0.0f;
    }
    rd.depth.texture = context.graph->ResolvedTexture(self->targets_.depth);
    rd.depth.load = rhi::LoadOp::Clear;
    rd.depth.store = rhi::StoreOp::Store;
    rd.depth.clear_depth = 1.0f;
    list->BeginRendering(rd);
    list->SetViewport(rhi::Viewport{0.0f, 0.0f, static_cast<f32>(self->width_), static_cast<f32>(self->height_), 0.0f, 1.0f});
    list->SetScissor(rhi::ScissorRect{0, 0, self->width_, self->height_});
    if (self->draw_count_ > 0) {
        list->SetBindGroup(kFrameSet, self->frame_group_);
        list->SetBindGroup(kObjectSet, self->object_group_);
    }
    rhi::PipelineHandle pipeline;
    rhi::BindGroupHandle group;
    for (u32 i = 0; i < self->draw_count_; ++i) {
        const Draw& d = self->draws_[i];
        if (!(d.pipeline == pipeline)) {
            list->SetPipeline(d.pipeline);
            pipeline = d.pipeline;
            group = rhi::BindGroupHandle{}; // a new pipeline layout: rebind the material group
        }
        if (!(d.material_group == group)) {
            list->SetBindGroup(kMaterialSet, d.material_group);
            group = d.material_group;
        }
        d.mesh->Draw(list, d.instance_count, d.first_instance);
    }
    list->EndRendering();
}

// ---- GBufferDebugView ----

GBufferDebugView::GBufferDebugView(rhi::Device* device) : device_(device) {
    ENGINE_ASSERT(device != nullptr, "GBufferDebugView: device is null");
    rhi::ShaderDesc sd;
    sd.stage = rhi::ShaderStage::Vertex;
    sd.words = shaders::kFullscreenVertSpv;
    sd.word_count = shaders::kFullscreenVertSpvWordCount;
    vertex_ = device->CreateShader(sd);
    sd.stage = rhi::ShaderStage::Fragment;
    sd.words = shaders::kGbufferDebugFragSpv;
    sd.word_count = shaders::kGbufferDebugFragSpvWordCount;
    fragment_ = device->CreateShader(sd);
    rhi::SamplerDesc smp;
    smp.mag_filter = rhi::FilterMode::Nearest;
    smp.min_filter = rhi::FilterMode::Nearest;
    smp.mip_filter = rhi::FilterMode::Nearest;
    smp.address_u = rhi::AddressMode::ClampToEdge;
    smp.address_v = rhi::AddressMode::ClampToEdge;
    sampler_ = device->CreateSampler(smp);
    rhi::BindingDesc bindings[5];
    for (u32 i = 0; i < 4; ++i) bindings[i] = {i, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment, 1};
    bindings[4] = {4, rhi::BindingType::Sampler, rhi::ShaderStage::Fragment, 1};
    rhi::BindGroupLayoutDesc ld;
    ld.entries = rhi::Span<rhi::BindingDesc>(bindings, 5);
    layout_ = device->CreateBindGroupLayout(ld);
}

GBufferDebugView::~GBufferDebugView() {
    if (pipeline_.IsValid()) device_->DestroyPipeline(pipeline_);
    if (layout_.IsValid()) device_->DestroyBindGroupLayout(layout_);
    if (sampler_.IsValid()) device_->DestroySampler(sampler_);
    if (fragment_.IsValid()) device_->DestroyShader(fragment_);
    if (vertex_.IsValid()) device_->DestroyShader(vertex_);
}

bool GBufferDebugView::Valid() const { return vertex_.IsValid() && fragment_.IsValid() && sampler_.IsValid() && layout_.IsValid(); }

void GBufferDebugView::AddToGraph(RenderGraph* graph, const GBufferTargets& targets, GBufferLayer layer, GraphResource output, rhi::Format output_format, f32 near_plane, f32 far_plane) {
    ENGINE_ASSERT(graph != nullptr && output_format != rhi::Format::Undefined, "GBufferDebugView::AddToGraph: graph is null or the output format is undefined");
    targets_ = targets;
    output_ = output;
    output_format_ = output_format;
    if (layer == GBufferLayer::WorldPosition) ENGINE_ASSERT(has_inverse_view_projection_, "GBufferDebugView::AddToGraph: the WorldPosition layer needs SetInverseViewProjection first");
    has_inverse_view_projection_ = false;
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
        gp.push_constant_bytes = sizeof(Params);
        gp.push_constant_stages = rhi::ShaderStage::Fragment;
        gp.debug_name = "gbuffer_debug";
        pipeline_ = device_->CreateGraphicsPipeline(gp);
        pipeline_format_ = pipeline_.IsValid() ? output_format_ : rhi::Format::Undefined;
    }
    params_.layer = static_cast<u32>(layer);
    params_.near_plane = near_plane;
    params_.far_plane = far_plane;
    graph->AddPass("gbuffer_debug", &GBufferDebugView::Record, this)
        .Read(targets.albedo, GraphUsage::Sampled)
        .Read(targets.normal, GraphUsage::Sampled)
        .Read(targets.material, GraphUsage::Sampled)
        .Read(targets.depth, GraphUsage::Sampled)
        .Write(output, GraphUsage::ColorAttachment);
}

void GBufferDebugView::Record(const PassExecuteContext& context, void* user) {
    GBufferDebugView* self = static_cast<GBufferDebugView*>(user);
    rhi::Device* device = self->device_;
    const rhi::TextureHandle out = context.graph->ResolvedTexture(self->output_);
    const rhi::TextureDesc& gd0 = context.graph->TextureDescOf(self->targets_.albedo);
    if (!self->pipeline_.IsValid()) {
        RenderLog(LogVerbosity::Error, "GBufferDebugView: the pipeline for the output format could not be created; nothing is drawn");
        return;
    }
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
    e[4].sampler = self->sampler_;
    rhi::BindGroupDesc gd;
    gd.layout = self->layout_;
    gd.entries = rhi::Span<rhi::BindGroupEntry>(e, 5);
    gd.transient = true;
    const rhi::BindGroupHandle group = device->CreateBindGroup(gd);
    rhi::RenderingDesc rd;
    rd.color_count = 1;
    rd.color[0].texture = out;
    rd.color[0].load = rhi::LoadOp::Clear;
    context.list->BeginRendering(rd);
    context.list->SetPipeline(self->pipeline_);
    context.list->SetViewport(rhi::Viewport{0.0f, 0.0f, static_cast<f32>(gd0.width), static_cast<f32>(gd0.height), 0.0f, 1.0f});
    context.list->SetScissor(rhi::ScissorRect{0, 0, gd0.width, gd0.height});
    context.list->SetBindGroup(0, group);
    context.list->SetPushConstants(0, &self->params_, sizeof(Params));
    context.list->Draw(3, 1, 0, 0);
    context.list->EndRendering();
}

} // namespace engine_placeholder::renderer
