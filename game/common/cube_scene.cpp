#include "lang/new.h"
#include "cube_scene.h"

#include "cube.frag.dxil.inc"
#include "cube.vert.refl.inc"
#include "cube.vert.dxil.inc"
#include "lit.frag.refl.inc"
#include "lit.frag.dxil.inc"
#include "lit.vert.refl.inc"
#include "lit.vert.dxil.inc"
#include "textured.frag.dxil.inc"
#include "textured.vert.dxil.inc"
#include "log/assert.h"
#include "rhi/rhi_log.h"
#include "cstr/mem_ops.h"

namespace demo
{

using namespace engine_placeholder::rhi;

namespace
{

struct Face
{
    f32 normal[3];
    f32 u[3];
    f32 v[3];  // u x v == normal, so the corners below run counter-clockwise seen from outside
};

const Face kFaces[6] = {
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},  {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},
    {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
};

struct CubeData
{
    CubeVertex vertices[kCubeVertexCount];
    u16 indices[kCubeIndexCount];
    CubeData()
    {
        const f32 corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        const f32 uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (u32 f = 0; f < 6; ++f)
        {
            for (u32 c = 0; c < 4; ++c)
            {
                CubeVertex& vtx = vertices[f * 4 + c];
                for (u32 k = 0; k < 3; ++k)
                {
                    vtx.position[k] = 0.5f * (kFaces[f].normal[k] + corner[c][0] * kFaces[f].u[k] +
                                              corner[c][1] * kFaces[f].v[k]);
                    vtx.normal[k] = kFaces[f].normal[k];
                }
                vtx.uv[0] = uv[c][0];
                vtx.uv[1] = uv[c][1];
            }
            const u16 base = static_cast<u16>(f * 4);
            const u16 quad[6] = {0, 1, 2, 0, 2, 3};
            for (u32 i = 0; i < 6; ++i)
            {
                indices[f * 6 + i] = static_cast<u16>(base + quad[i]);
            }
        }
    }
};

const CubeData& Cube()
{
    static const CubeData data;
    return data;
}

}  // namespace

const CubeVertex* CubeVertices()
{
    return Cube().vertices;
}
const u16* CubeIndices()
{
    return Cube().indices;
}

bool CubeRenderer::Create(Device* device, const CubeOptions& options)
{
    device_ = device;
    indexed_ = options.indexed;
    lit_ = options.lit;
    textured_ = options.textured || options.lit;
    vs_ = CreateShaderFromSet(*device,
                              lit_        ? shaders::kLitVertShader
                              : textured_ ? shaders::kTexturedVertShader
                                          : shaders::kCubeVertShader,
                              ShaderStage::Vertex);
    fs_ = CreateShaderFromSet(*device,
                              lit_        ? shaders::kLitFragShader
                              : textured_ ? shaders::kTexturedFragShader
                                          : shaders::kCubeFragShader,
                              ShaderStage::Fragment);

    BindingDesc bindings[3] = {
        {0, BindingType::UniformBuffer, lit_ ? ShaderStage::Vertex | ShaderStage::Fragment : ShaderStage::Vertex, 1},
        {1, BindingType::SampledTexture, ShaderStage::Fragment, 1},
        {2, BindingType::Sampler, ShaderStage::Fragment, 1}};
    BindGroupLayoutDesc ld;
    ld.entries = rhi::Span<BindingDesc>(bindings, textured_ ? 3 : 1);
    layout_ = device->CreateBindGroupLayout(ld);

    VertexBindingDesc vertex_binding = {sizeof(CubeVertex), false};
    VertexAttributeDesc attributes[3] = {
        {0, 0, VertexFormat::Float3, 0}, {1, 0, VertexFormat::Float3, 12}, {2, 0, VertexFormat::Float2, 24}};
    GraphicsPipelineDesc gp;
    gp.vertex_shader = vs_;
    gp.fragment_shader = fs_;
    gp.vertex_bindings = rhi::Span<VertexBindingDesc>(&vertex_binding, 1);
    gp.vertex_attributes = rhi::Span<VertexAttributeDesc>(attributes, 3);
    gp.cull_mode = options.cull;
    gp.front_face = FrontFace::CounterClockwise;  // see the conventions at the top of cube_scene.h
    gp.color_targets[0].format = options.color_format;
    gp.color_target_count = 1;
    gp.depth_format = options.depth_format;
    gp.depth_test = options.depth_format != Format::Undefined && options.depth_test;
    gp.depth_write = gp.depth_test;
    gp.bind_group_layouts[0] = layout_;
    gp.bind_group_layout_count = 1;
    gp.debug_name = "cube";
    pipeline_ = device->CreateGraphicsPipeline(gp);

    BufferDesc bd;
    bd.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
    bd.location = MemoryLocation::GpuOnly;
    bool ok = vs_.IsValid() && fs_.IsValid() && layout_.IsValid() && pipeline_.IsValid();
    if (ok)
    {
        if (indexed_)
        {
            bd.size = sizeof(CubeVertex) * kCubeVertexCount;
            bd.debug_name = "cube vertices";
            vertices_ = device->CreateBuffer(bd);
            bd.size = sizeof(u16) * kCubeIndexCount;
            bd.usage = BufferUsage::Index | BufferUsage::CopyDst;
            bd.debug_name = "cube indices";
            indices_ = device->CreateBuffer(bd);
            ok = vertices_.IsValid() && indices_.IsValid() &&
                 device->UploadBuffer(vertices_, 0, CubeVertices(), sizeof(CubeVertex) * kCubeVertexCount).ok &&
                 device->UploadBuffer(indices_, 0, CubeIndices(), sizeof(u16) * kCubeIndexCount).ok;
        }
        else
        {
            CubeVertex expanded[kCubeIndexCount];
            for (u32 i = 0; i < kCubeIndexCount; ++i)
            {
                expanded[i] = CubeVertices()[CubeIndices()[i]];
            }
            bd.size = sizeof(expanded);
            bd.debug_name = "cube vertices (expanded)";
            vertices_ = device->CreateBuffer(bd);
            ok = vertices_.IsValid() && device->UploadBuffer(vertices_, 0, expanded, sizeof(expanded)).ok;
        }
    }
    if (!ok)
    {
        Destroy();
        device_ = nullptr;
    }
    return ok;
}

void CubeRenderer::Destroy()
{
    if (device_ == nullptr)
    {
        return;
    }
    if (indices_.IsValid())
    {
        device_->DestroyBuffer(indices_);
    }
    if (vertices_.IsValid())
    {
        device_->DestroyBuffer(vertices_);
    }
    if (pipeline_.IsValid())
    {
        device_->DestroyPipeline(pipeline_);
    }
    if (layout_.IsValid())
    {
        device_->DestroyBindGroupLayout(layout_);
    }
    if (vs_.IsValid())
    {
        device_->DestroyShader(vs_);
    }
    if (fs_.IsValid())
    {
        device_->DestroyShader(fs_);
    }
    indices_ = {};
    vertices_ = {};
    pipeline_ = {};
    layout_ = {};
    vs_ = {};
    fs_ = {};
    geometry_ready_ = false;  // a renderer created again has fresh buffers, still in their upload state
    device_ = nullptr;
}

void CubeRenderer::Prepare(CommandList* list)
{
    if (geometry_ready_)
    {
        return;
    }
    list->Barrier(vertices_, ResourceState::CopyDst, ResourceState::VertexBuffer);
    if (indexed_)
    {
        list->Barrier(indices_, ResourceState::CopyDst, ResourceState::IndexBuffer);
    }
}

bool CubeRenderer::Draw(CommandList* list, UniformRing* ring, const Mat4& mvp, u32 width, u32 height)
{
    ENGINE_ASSERT(!lit_, "CubeRenderer::Draw: a lit renderer is drawn with DrawLit");
    const UniformAllocation u = ring->Allocate(sizeof(Mat4));
    if (!u.Valid())
    {
        return false;
    }
    MemCopy(u.data, &mvp, sizeof(Mat4));
    return DrawWith(list, u, width, height);
}

namespace
{

// The uniform block of shaders/lit.vert and lit.frag (std140: every member is a vec4 or a mat4).
struct LitUniforms
{
    Mat4 mvp;
    Mat4 normal_matrix;
    Vec4 to_light;
    Vec4 light_color;
    Vec4 ambient;
};
// The expected values are generated from the shaders' reflection (shaders/*.refl.inc), not typed here, so changing a
// block in a .slang file without changing this struct fails the build.
static_assert(sizeof(LitUniforms) == shaders::kLitVertParamsSize &&
                  ENGINE_OFFSET_OF(LitUniforms, mvp) == shaders::kLitVertParamsMvpOffset &&
                  ENGINE_OFFSET_OF(LitUniforms, normal_matrix) == shaders::kLitVertParamsNormalMatrixOffset &&
                  ENGINE_OFFSET_OF(LitUniforms, to_light) == shaders::kLitVertParamsToLightOffset &&
                  ENGINE_OFFSET_OF(LitUniforms, light_color) == shaders::kLitVertParamsLightColorOffset &&
                  ENGINE_OFFSET_OF(LitUniforms, ambient) == shaders::kLitVertParamsAmbientOffset,
              "LitUniforms must match the std140 block in shaders/lit.vert.slang");
static_assert(shaders::kLitFragParamsSize == shaders::kLitVertParamsSize, "lit.vert and lit.frag share one block");
static_assert(sizeof(Mat4) == shaders::kCubeVertParamsSize && shaders::kCubeVertParamsMvpOffset == 0,
              "the cube shaders' block is one Mat4");

}  // namespace

bool CubeRenderer::DrawLit(CommandList* list, UniformRing* ring, const Mat4& view_projection, const Mat4& model,
                           const Lighting& lighting, u32 width, u32 height)
{
    ENGINE_ASSERT(lit_, "CubeRenderer::DrawLit: the renderer was not created with `lit`");
    const UniformAllocation u = ring->Allocate(sizeof(LitUniforms));
    if (!u.Valid())
    {
        return false;
    }
    LitUniforms block;
    block.mvp = view_projection * model;
    block.normal_matrix = NormalMatrix(model);
    const Vec3& direction = lighting.to_light;
    const Vec3 l = direction.LengthSquared() > 1e-12f ? direction.Normalized()
                                                      : Vec3(0, 0, 0);  // zero: no direct light
    block.to_light = Vec4(l.x, l.y, l.z, 0.0f);
    block.light_color = Vec4(lighting.color.x * lighting.intensity, lighting.color.y * lighting.intensity,
                             lighting.color.z * lighting.intensity, 0.0f);
    block.ambient = Vec4(lighting.ambient.x, lighting.ambient.y, lighting.ambient.z, 0.0f);
    MemCopy(u.data, &block, sizeof(block));
    return DrawWith(list, u, width, height);
}

bool CubeRenderer::DrawWith(CommandList* list, const UniformAllocation& u, u32 width, u32 height)
{
    BindGroupEntry entries[3];
    entries[0].binding = 0;
    entries[0].buffer = u.buffer;
    entries[0].offset = u.offset;
    entries[0].size = u.size;
    entries[1].binding = 1;
    entries[1].texture = texture_;
    entries[2].binding = 2;
    entries[2].sampler = sampler_;
    BindGroupDesc gd;
    gd.layout = layout_;
    gd.entries = rhi::Span<BindGroupEntry>(entries, textured_ ? 3 : 1);
    gd.transient = true;
    const BindGroupHandle group = device_->CreateBindGroup(gd);
    if (!group.IsValid())
    {
        return false;
    }
    list->SetPipeline(pipeline_);
    list->SetViewport(Viewport{0, 0, static_cast<f32>(width), static_cast<f32>(height), 0.0f, 1.0f});
    list->SetScissor(ScissorRect{0, 0, width, height});
    list->SetBindGroup(0, group);
    list->SetVertexBuffer(0, vertices_, 0);
    if (indexed_)
    {
        list->SetIndexBuffer(indices_, 0, IndexType::UInt16);
        list->DrawIndexed(kCubeIndexCount, 1, 0, 0, 0);
    }
    else
    {
        list->Draw(kCubeIndexCount, 1, 0, 0);
    }
    return true;
}

}  // namespace demo
