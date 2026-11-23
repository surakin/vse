#include "renderer/sky.h"

#include "fullscreen.vert.dxil.inc"
#include "lang/scalar.h"
#include "log/assert.h"
#include "renderer/renderer_log.h"
#include "sky.frag.dxil.inc"

namespace vse::renderer
{

namespace
{

constexpr f32 kSkyHalfPi = 1.57079632679489661923f;
// The sun's edge is soft over this fraction of its size, outside the disc.
constexpr f32 kSkySoftEdge = 0.25f;

f32 SkySaturate(f32 v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

f32 SkySmooth(f32 x)
{
    x = SkySaturate(x);
    return x * x * (3.0f - 2.0f * x);
}

f32 SkySmoothStep(f32 edge0, f32 edge1, f32 x)
{
    return SkySmooth((x - edge0) / (edge1 - edge0));
}

Vec3 SkyLerp(const Vec3& a, const Vec3& b, f32 t)
{
    return a + (b - a) * t;
}

bool SkyFinite(const Vec3& v)
{
    return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
}

bool SkyNonNegative(const Vec3& v)
{
    return SkyFinite(v) && v.x >= 0.0f && v.y >= 0.0f && v.z >= 0.0f;
}

}  // namespace

bool SkySettingsValid(const SkySettings& s)
{
    return SkyNonNegative(s.color) && SkyNonNegative(s.zenith) && SkyNonNegative(s.horizon) &&
           SkyNonNegative(s.ground) && SkyNonNegative(s.sun_color) && SkyFinite(s.sun_direction) &&
           s.sun_direction.LengthSquared() > 1e-12f && IsFinite(s.sun_size) && s.sun_size > 0.0f &&
           s.sun_size <= kSkyHalfPi && IsFinite(s.horizon_blend) && s.horizon_blend > 0.0f && s.horizon_blend <= 1.0f &&
           IsFinite(s.exposure) && s.exposure > 0.0f && IsFinite(s.rotation);
}

Vec3 SkyColor(const SkySettings& s, const Vec3& direction)
{
    if (s.mode == SkyMode::None)
    {
        return Vec3(0.0f, 0.0f, 0.0f);
    }
    if (s.mode == SkyMode::Color)
    {
        return s.color;
    }
    const Vec3 d = direction.Normalized();
    const f32 e = d.y;
    Vec3 color = e >= 0.0f ? SkyLerp(s.horizon, s.zenith, SkySmooth(e))
                           : SkyLerp(s.horizon, s.ground, SkySmooth(-e / s.horizon_blend));
    const f32 cos_inner = Cos(s.sun_size);
    const f32 cos_outer = Cos(s.sun_size * (1.0f + kSkySoftEdge));
    const f32 c = d.Dot(s.sun_direction.Normalized());
    color = color + s.sun_color * SkySmoothStep(cos_outer, cos_inner, c);
    return color * s.exposure;
}

SkyPass::SkyPass(rhi::Device* device, MaterialSystem* materials, FrameResources* frame)
    : device_(device), materials_(materials), frame_(frame)
{
    ENGINE_ASSERT(device != nullptr && materials != nullptr && frame != nullptr,
                  "SkyPass: device, materials or frame is null");
    Build();
}

void SkyPass::Build()
{
    rhi::Device* device = device_;
    vertex_ = rhi::CreateShaderFromSet(*device, shaders::kFullscreenVertShader, rhi::ShaderStage::Vertex);
    fragment_ = rhi::CreateShaderFromSet(*device, shaders::kSkyFragShader, rhi::ShaderStage::Fragment);
    rhi::BindingDesc binding = {0, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment, 1};
    rhi::BindGroupLayoutDesc ld;
    ld.entries = rhi::Span<rhi::BindingDesc>(&binding, 1);
    layout_ = device->CreateBindGroupLayout(ld);
    if (!vertex_.IsValid() || !fragment_.IsValid() || !layout_.IsValid())
    {
        RenderLog(LogVerbosity::Warn, "SkyPass: creating the shaders or the bind group layout failed; no sky is drawn");
        return;
    }
    rhi::GraphicsPipelineDesc gp;
    gp.vertex_shader = vertex_;
    gp.fragment_shader = fragment_;
    gp.cull_mode = rhi::CullMode::None;
    gp.color_targets[0].format = kLightAccumFormat;
    gp.color_target_count = 1;
    gp.bind_group_layouts[0] = materials_->FrameLayout();
    gp.bind_group_layouts[1] = layout_;
    gp.bind_group_layout_count = 2;
    gp.push_constant_bytes = sizeof(Push);
    gp.push_constant_stages = rhi::ShaderStage::Fragment;
    gp.debug_name = "sky";
    pipeline_ = device->CreateGraphicsPipeline(gp);
    if (!pipeline_.IsValid())
    {
        RenderLog(LogVerbosity::Warn, "SkyPass: creating the pipeline failed; no sky is drawn");
    }
}

SkyPass::~SkyPass()
{
    registration_.Reset();
    Release();
}

void SkyPass::Release()
{
    if (device_ == nullptr)
    {
        return;
    }
    if (pipeline_.IsValid())
    {
        device_->DestroyPipeline(pipeline_);
    }
    if (layout_.IsValid())
    {
        device_->DestroyBindGroupLayout(layout_);
    }
    if (fragment_.IsValid())
    {
        device_->DestroyShader(fragment_);
    }
    if (vertex_.IsValid())
    {
        device_->DestroyShader(vertex_);
    }
    pipeline_ = {};
    layout_ = {};
    fragment_ = {};
    vertex_ = {};
    frame_group_ = {};  // a handle from the old device; Prepare fetches it again
}

bool SkyPass::Recreate(rhi::Device* device)
{
    device_ = device;
    Build();
    // A device that has no shader for the sky (a backend whose shader library is not minted yet) draws no sky; that is
    // not a failure of the reset.
    if (!pipeline_.IsValid())
    {
        Release();
    }
    return true;
}

void SkyPass::Prepare(const SkySettings& s)
{
    ENGINE_ASSERT(SkySettingsValid(s), "SkyPass::Prepare: the sky settings are invalid");
    draw_ = s.mode == SkyMode::Procedural && pipeline_.IsValid();
    if (!draw_)
    {
        return;
    }
    const Vec3 sun = s.sun_direction.Normalized();
    const Vec3* colors[3] = {&s.zenith, &s.horizon, &s.ground};
    f32* out[3] = {push_.zenith, push_.horizon, push_.ground};
    for (int i = 0; i < 3; ++i)
    {
        out[i][0] = colors[i]->x;
        out[i][1] = colors[i]->y;
        out[i][2] = colors[i]->z;
    }
    push_.sun[0] = sun.x;
    push_.sun[1] = sun.y;
    push_.sun[2] = sun.z;
    push_.sun[3] = s.exposure;
    push_.sun_color[0] = s.sun_color.x;
    push_.sun_color[1] = s.sun_color.y;
    push_.sun_color[2] = s.sun_color.z;
    push_.sun_color[3] = 0.0f;
    push_.zenith[3] = Cos(s.sun_size);
    push_.horizon[3] = Cos(s.sun_size * (1.0f + kSkySoftEdge));
    push_.ground[3] = s.horizon_blend;
    frame_group_ = materials_->FrameBindGroup(frame_->FrameRegion());
}

bool SkyPass::AddToGraph(RenderGraph* graph, const GBufferTargets& targets, GraphResource accum)
{
    ENGINE_ASSERT(graph != nullptr, "SkyPass::AddToGraph: graph is null");
    if (!draw_)
    {
        return false;
    }
    targets_ = targets;
    accum_ = accum;
    graph->AddPass("sky", &SkyPass::Record, this)
        .Read(targets.depth, GraphUsage::Sampled)
        .ReadWrite(accum, GraphUsage::ColorAttachment);
    return true;
}

void SkyPass::Record(const PassExecuteContext& context, void* user)
{
    SkyPass* self = static_cast<SkyPass*>(user);
    if (!self->pipeline_.IsValid() || !self->frame_group_.IsValid())
    {
        RenderLog(LogVerbosity::Error, "SkyPass: the pipeline or the frame bind group is missing; no sky is drawn");
        return;
    }
    const rhi::TextureDesc& desc = context.graph->TextureDescOf(self->accum_);
    rhi::BindGroupEntry e;
    e.binding = 0;
    e.texture = context.graph->ResolvedTexture(self->targets_.depth);
    rhi::BindGroupDesc gd;
    gd.layout = self->layout_;
    gd.entries = rhi::Span<rhi::BindGroupEntry>(&e, 1);
    gd.transient = true;
    const rhi::BindGroupHandle group = self->device_->CreateBindGroup(gd);
    rhi::RenderingDesc rd;
    rd.color_count = 1;
    rd.color[0].texture = context.graph->ResolvedTexture(self->accum_);
    rd.color[0].load = rhi::LoadOp::Load;
    rhi::CommandList* list = context.list;
    list->BeginRendering(rd);
    list->SetPipeline(self->pipeline_);
    list->SetViewport(
        rhi::Viewport{0.0f, 0.0f, static_cast<f32>(desc.width), static_cast<f32>(desc.height), 0.0f, 1.0f});
    list->SetScissor(rhi::ScissorRect{0, 0, desc.width, desc.height});
    list->SetBindGroup(kFrameSet, self->frame_group_);
    list->SetBindGroup(1, group);
    list->SetPushConstants(0, &self->push_, sizeof(Push));
    list->Draw(3, 1, 0, 0);
    list->EndRendering();
}

}  // namespace vse::renderer
