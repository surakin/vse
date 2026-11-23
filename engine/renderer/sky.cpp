#include "renderer/sky.h"

#include "fullscreen.vert.dxil.inc"
#include "containers/shared_bytes.h"
#include "cstr/mem_ops.h"
#include "lang/scalar.h"
#include "lang/utility.h"
#include "log/assert.h"
#include "renderer/lighting.h"
#include "renderer/renderer_log.h"
#include "sky.frag.dxil.inc"
#include "sky_cube.frag.dxil.inc"

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
           IsFinite(s.exposure) && s.exposure > 0.0f && IsFinite(s.ambient_intensity) && s.ambient_intensity >= 0.0f &&
           IsFinite(s.rotation) && SkyNonNegative(s.tint);
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

Vec3 SkyCubeDirection(const SkySettings& s, const Vec3& d)
{
    const f32 c = Cos(s.rotation);
    const f32 sn = Sin(s.rotation);
    return Vec3(c * d.x - sn * d.z, d.y, sn * d.x + c * d.z);
}

bool SkyEnvironmentSame(const SkySettings& a, const SkySettings& b)
{
    auto same = [](const Vec3& x, const Vec3& y) { return x.x == y.x && x.y == y.y && x.z == y.z; };
    return same(a.zenith, b.zenith) && same(a.horizon, b.horizon) && same(a.ground, b.ground) &&
           a.horizon_blend == b.horizon_blend && a.exposure == b.exposure;
}

namespace
{

constexpr u32 kSkyEnvironmentFace = 16;
constexpr u32 kSkyEnvironmentMips = 5;

// The direction of a face texel: the same table as asset::CubeFaceDirection (renderer cannot include the asset module).
Vec3 SkyFaceDirection(u32 face, f32 u, f32 v)
{
    switch (face)
    {
    case 0: return Vec3(1.0f, -v, -u);
    case 1: return Vec3(-1.0f, -v, u);
    case 2: return Vec3(u, 1.0f, v);
    case 3: return Vec3(u, -1.0f, -v);
    case 4: return Vec3(u, -v, 1.0f);
    default: return Vec3(-u, -v, -1.0f);
    }
}

// A finite non-negative f32 as the nearest half float (ties to even), clamped to the largest finite half.
u16 SkyHalfBits(f32 value)
{
    if (!(value > 0.0f))
    {
        return 0;
    }
    if (value >= 65504.0f)
    {
        return 0x7bffu;
    }
    u32 bits;
    MemCopy(&bits, &value, sizeof(bits));
    const u32 exponent = bits >> 23;
    if (exponent < 113)  // a subnormal half, or zero
    {
        const u32 mantissa = (bits & 0x7fffffu) | 0x800000u;
        if (exponent < 102)
        {
            return 0;
        }
        const u32 shift = 126 - exponent;
        u32 half = mantissa >> shift;
        const u32 rest = mantissa & ((1u << shift) - 1);
        const u32 half_way = 1u << (shift - 1);
        if (rest > half_way || (rest == half_way && (half & 1) != 0))
        {
            ++half;
        }
        return static_cast<u16>(half);
    }
    u32 half = ((exponent - 112) << 10) | ((bits >> 13) & 1023);
    const u32 rest = bits & 0x1fffu;
    if (rest > 0x1000u || (rest == 0x1000u && (half & 1) != 0))
    {
        ++half;
    }
    return static_cast<u16>(half);
}

}  // namespace

GpuTexturePtr MakeSkyEnvironmentCube(rhi::Device* device, DeviceResources* resources, const SkySettings& settings)
{
    ENGINE_ASSERT(device != nullptr, "MakeSkyEnvironmentCube: device is null");
    if (!device->Caps().cube_textures)
    {
        return GpuTexturePtr();
    }
    SkySettings sky = settings;
    sky.mode = SkyMode::Procedural;
    sky.sun_color = Vec3(0.0f, 0.0f, 0.0f);
    // The float chain of the six faces: level 0 from the gradient, each next level the mean of 2 x 2 texels of the one
    // before.
    Vector<f32> levels[kSkyEnvironmentMips][6];
    for (u32 face = 0; face < 6; ++face)
    {
        Vector<f32>& base = levels[0][face];
        base.Resize(static_cast<usize>(kSkyEnvironmentFace) * kSkyEnvironmentFace * 3);
        for (u32 y = 0; y < kSkyEnvironmentFace; ++y)
        {
            for (u32 x = 0; x < kSkyEnvironmentFace; ++x)
            {
                const f32 u = 2.0f * (static_cast<f32>(x) + 0.5f) / static_cast<f32>(kSkyEnvironmentFace) - 1.0f;
                const f32 v = 2.0f * (static_cast<f32>(y) + 0.5f) / static_cast<f32>(kSkyEnvironmentFace) - 1.0f;
                const Vec3 c = SkyColor(sky, SkyFaceDirection(face, u, v));
                f32* t = base.Data() + (static_cast<usize>(y) * kSkyEnvironmentFace + x) * 3;
                t[0] = c.x;
                t[1] = c.y;
                t[2] = c.z;
            }
        }
        for (u32 mip = 1; mip < kSkyEnvironmentMips; ++mip)
        {
            const u32 size = kSkyEnvironmentFace >> mip;
            const u32 parent = size * 2;
            Vector<f32>& level = levels[mip][face];
            level.Resize(static_cast<usize>(size) * size * 3);
            for (u32 y = 0; y < size; ++y)
            {
                for (u32 x = 0; x < size; ++x)
                {
                    for (u32 c = 0; c < 3; ++c)
                    {
                        const Vector<f32>& p = levels[mip - 1][face];
                        const f32 sum = p[((2 * y) * parent + 2 * x) * 3 + c] +
                                        p[((2 * y) * parent + 2 * x + 1) * 3 + c] +
                                        p[((2 * y + 1) * parent + 2 * x) * 3 + c] +
                                        p[((2 * y + 1) * parent + 2 * x + 1) * 3 + c];
                        level[(static_cast<usize>(y) * size + x) * 3 + c] = sum * 0.25f;
                    }
                }
            }
        }
    }
    Vector<byte> bytes;
    Vector<TextureLevelSpan> spans;
    for (u32 face = 0; face < 6; ++face)
    {
        for (u32 mip = 0; mip < kSkyEnvironmentMips; ++mip)
        {
            const u32 size = kSkyEnvironmentFace >> mip;
            TextureLevelSpan span;
            span.offset = bytes.Size();
            span.bytes = static_cast<u64>(size) * size * 8;
            span.row_pitch = size * 8;
            span.mip = mip;
            span.layer = face;
            spans.Push(span);
            const usize base = bytes.Size();
            bytes.Resize(base + static_cast<usize>(span.bytes));
            for (usize i = 0; i < static_cast<usize>(size) * size; ++i)
            {
                const u16 h[4] = {SkyHalfBits(levels[mip][face][i * 3]), SkyHalfBits(levels[mip][face][i * 3 + 1]),
                                  SkyHalfBits(levels[mip][face][i * 3 + 2]), 0x3C00u};
                for (u32 c = 0; c < 4; ++c)
                {
                    bytes[base + i * 8 + c * 2] = static_cast<byte>(h[c] & 0xffu);
                    bytes[base + i * 8 + c * 2 + 1] = static_cast<byte>(h[c] >> 8);
                }
            }
        }
    }
    rhi::TextureDesc desc;
    desc.dimension = rhi::TextureDimension::Cube;
    desc.width = kSkyEnvironmentFace;
    desc.height = kSkyEnvironmentFace;
    desc.array_layers = 6;
    desc.mip_levels = kSkyEnvironmentMips;
    desc.format = rhi::Format::RGBA16_Float;
    desc.usage = rhi::TextureUsage::Sampled;
    GpuTexturePtr cube = MakeShared<GpuTexture>();
    SharedBytesPtr region = MakeShared<OwnedBytes>(Move(bytes));
    if (!cube->CreateFromSharedLevels(device, desc, region, Span<const TextureLevelSpan>(spans.Data(), spans.Size()),
                                      resources, "sky environment"))
    {
        return GpuTexturePtr();
    }
    return cube;
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
        return;
    }
    if (!device->Caps().cube_textures)
    {
        return;  // the cube sky falls back to the procedural one
    }
    cube_fragment_ = rhi::CreateShaderFromSet(*device, shaders::kSkyCubeFragShader, rhi::ShaderStage::Fragment);
    rhi::BindingDesc cube_bindings[3] = {{0, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment, 1},
                                         {1, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment, 1},
                                         {2, rhi::BindingType::Sampler, rhi::ShaderStage::Fragment, 1}};
    rhi::BindGroupLayoutDesc cld;
    cld.entries = rhi::Span<rhi::BindingDesc>(cube_bindings, 3);
    cube_layout_ = device->CreateBindGroupLayout(cld);
    rhi::SamplerDesc sd;
    sd.address_u = sd.address_v = sd.address_w = rhi::AddressMode::ClampToEdge;
    sd.debug_name = "sky cube";
    sampler_ = device->CreateSampler(sd);
    if (cube_fragment_.IsValid() && cube_layout_.IsValid() && sampler_.IsValid())
    {
        rhi::GraphicsPipelineDesc cp = gp;
        cp.fragment_shader = cube_fragment_;
        cp.bind_group_layouts[1] = cube_layout_;
        cp.push_constant_bytes = sizeof(CubePush);
        cp.debug_name = "sky cube";
        cube_pipeline_ = device->CreateGraphicsPipeline(cp);
    }
    if (!cube_pipeline_.IsValid())
    {
        RenderLog(LogVerbosity::Warn,
                  "SkyPass: the cube sky is not available on this device; the procedural sky is used");
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
    if (cube_pipeline_.IsValid())
    {
        device_->DestroyPipeline(cube_pipeline_);
    }
    if (sampler_.IsValid())
    {
        device_->DestroySampler(sampler_);
    }
    if (cube_layout_.IsValid())
    {
        device_->DestroyBindGroupLayout(cube_layout_);
    }
    if (cube_fragment_.IsValid())
    {
        device_->DestroyShader(cube_fragment_);
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
    cube_pipeline_ = {};
    sampler_ = {};
    cube_layout_ = {};
    cube_fragment_ = {};
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
    cube_.Reset();
    draw_cube_ = false;
    draw_ = (s.mode == SkyMode::Procedural || s.mode == SkyMode::Cube) && pipeline_.IsValid();
    if (!draw_)
    {
        return;
    }
    if (s.mode == SkyMode::Cube && cube_pipeline_.IsValid() && s.cube && s.cube->Valid() &&
        s.cube->Desc().dimension == rhi::TextureDimension::Cube)
    {
        draw_cube_ = true;
        cube_ = s.cube;
        cube_push_.tint_exposure[0] = s.tint.x * s.exposure;
        cube_push_.tint_exposure[1] = s.tint.y * s.exposure;
        cube_push_.tint_exposure[2] = s.tint.z * s.exposure;
        cube_push_.tint_exposure[3] = 0.0f;
        cube_push_.yaw[0] = Cos(s.rotation);
        cube_push_.yaw[1] = Sin(s.rotation);
        cube_push_.yaw[2] = 0.0f;
        cube_push_.yaw[3] = 0.0f;
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
    const bool cube = self->draw_cube_ && self->cube_ && self->cube_->Valid();
    rhi::BindGroupEntry e[3];
    e[0].binding = 0;
    e[0].texture = context.graph->ResolvedTexture(self->targets_.depth);
    if (cube)
    {
        e[1].binding = 1;
        e[1].texture = self->cube_->Handle();  // read now: a reset may have remade it
        e[2].binding = 2;
        e[2].sampler = self->sampler_;
    }
    rhi::BindGroupDesc gd;
    gd.layout = cube ? self->cube_layout_ : self->layout_;
    gd.entries = rhi::Span<rhi::BindGroupEntry>(e, cube ? 3 : 1);
    gd.transient = true;
    const rhi::BindGroupHandle group = self->device_->CreateBindGroup(gd);
    rhi::RenderingDesc rd;
    rd.color_count = 1;
    rd.color[0].texture = context.graph->ResolvedTexture(self->accum_);
    rd.color[0].load = rhi::LoadOp::Load;
    rhi::CommandList* list = context.list;
    list->BeginRendering(rd);
    list->SetPipeline(cube ? self->cube_pipeline_ : self->pipeline_);
    list->SetViewport(
        rhi::Viewport{0.0f, 0.0f, static_cast<f32>(desc.width), static_cast<f32>(desc.height), 0.0f, 1.0f});
    list->SetScissor(rhi::ScissorRect{0, 0, desc.width, desc.height});
    list->SetBindGroup(kFrameSet, self->frame_group_);
    list->SetBindGroup(1, group);
    if (cube)
    {
        list->SetPushConstants(0, &self->cube_push_, sizeof(CubePush));
    }
    else
    {
        list->SetPushConstants(0, &self->push_, sizeof(Push));
    }
    list->Draw(3, 1, 0, 0);
    list->EndRendering();
}

}  // namespace vse::renderer
