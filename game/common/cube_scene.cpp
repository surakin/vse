#include "cube_scene.h"

#include <cmath>
#include <cstring>

#include "cube.frag.spv.inc"
#include "cube.vert.spv.inc"
#include "math/quaternion.h"
#include "rhi/rhi_log.h"

namespace demo {

using namespace engine_placeholder::rhi;

Mat4 PerspectiveVulkan(f32 fov_y, f32 aspect, f32 near_plane, f32 far_plane) {
    const f32 f = 1.0f / std::tan(fov_y * 0.5f);
    const f32 range = near_plane - far_plane;
    return Mat4::FromColumns(Vec4(f / aspect, 0, 0, 0), Vec4(0, -f, 0, 0), Vec4(0, 0, far_plane / range, -1),
                             Vec4(0, 0, near_plane * far_plane / range, 0));
}

Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 f = (target - eye).Normalized();
    const Vec3 s = f.Cross(up).Normalized();
    const Vec3 u = s.Cross(f);
    return Mat4::FromColumns(Vec4(s.x, u.x, -f.x, 0), Vec4(s.y, u.y, -f.y, 0), Vec4(s.z, u.z, -f.z, 0),
                             Vec4(-s.Dot(eye), -u.Dot(eye), f.Dot(eye), 1));
}

Mat4 ModelMatrix(const Vec3& position, f32 scale, f32 yaw_radians) {
    const Mat3 r = Quaternion::FromAxisAngle(Vec3(0, 1, 0), yaw_radians).ToMat3();
    return Mat4::FromBasis(r.col[0] * scale, r.col[1] * scale, r.col[2] * scale, position);
}

namespace {

struct Face {
    f32 normal[3];
    f32 u[3];
    f32 v[3]; // u x v == normal, so the corners below run counter-clockwise seen from outside
};

const Face kFaces[6] = {
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},  {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},
    {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
};

struct CubeData {
    CubeVertex vertices[kCubeVertexCount];
    u16 indices[kCubeIndexCount];
    CubeData() {
        const f32 corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        const f32 uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (u32 f = 0; f < 6; ++f) {
            for (u32 c = 0; c < 4; ++c) {
                CubeVertex& vtx = vertices[f * 4 + c];
                for (u32 k = 0; k < 3; ++k) {
                    vtx.position[k] = 0.5f * (kFaces[f].normal[k] + corner[c][0] * kFaces[f].u[k] + corner[c][1] * kFaces[f].v[k]);
                    vtx.normal[k] = kFaces[f].normal[k];
                }
                vtx.uv[0] = uv[c][0];
                vtx.uv[1] = uv[c][1];
            }
            const u16 base = static_cast<u16>(f * 4);
            const u16 quad[6] = {0, 1, 2, 0, 2, 3};
            for (u32 i = 0; i < 6; ++i) {
                indices[f * 6 + i] = static_cast<u16>(base + quad[i]);
            }
        }
    }
};

const CubeData& Cube() {
    static const CubeData data;
    return data;
}

} // namespace

const CubeVertex* CubeVertices() { return Cube().vertices; }
const u16* CubeIndices() { return Cube().indices; }

bool CubeRenderer::Create(Device* device, const CubeOptions& options) {
    device_ = device;
    indexed_ = options.indexed;
    ShaderDesc sd;
    sd.stage = ShaderStage::Vertex;
    sd.words = shaders::kCubeVertSpv;
    sd.word_count = shaders::kCubeVertSpvWordCount;
    vs_ = device->CreateShader(sd);
    sd.stage = ShaderStage::Fragment;
    sd.words = shaders::kCubeFragSpv;
    sd.word_count = shaders::kCubeFragSpvWordCount;
    fs_ = device->CreateShader(sd);

    BindingDesc binding = {0, BindingType::UniformBuffer, ShaderStage::Vertex, 1};
    BindGroupLayoutDesc ld;
    ld.entries = Span<BindingDesc>(&binding, 1);
    layout_ = device->CreateBindGroupLayout(ld);

    VertexBindingDesc vertex_binding = {sizeof(CubeVertex), false};
    VertexAttributeDesc attributes[3] = {{0, 0, VertexFormat::Float3, 0}, {1, 0, VertexFormat::Float3, 12}, {2, 0, VertexFormat::Float2, 24}};
    GraphicsPipelineDesc gp;
    gp.vertex_shader = vs_;
    gp.fragment_shader = fs_;
    gp.vertex_bindings = Span<VertexBindingDesc>(&vertex_binding, 1);
    gp.vertex_attributes = Span<VertexAttributeDesc>(attributes, 3);
    gp.cull_mode = options.cull;
    gp.front_face = FrontFace::CounterClockwise; // see the conventions at the top of cube_scene.h
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
    if (ok) {
        if (indexed_) {
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
        } else {
            CubeVertex expanded[kCubeIndexCount];
            for (u32 i = 0; i < kCubeIndexCount; ++i) {
                expanded[i] = CubeVertices()[CubeIndices()[i]];
            }
            bd.size = sizeof(expanded);
            bd.debug_name = "cube vertices (expanded)";
            vertices_ = device->CreateBuffer(bd);
            ok = vertices_.IsValid() && device->UploadBuffer(vertices_, 0, expanded, sizeof(expanded)).ok;
        }
    }
    if (!ok) {
        Destroy();
        device_ = nullptr;
    }
    return ok;
}

void CubeRenderer::Destroy() {
    if (device_ == nullptr) {
        return;
    }
    if (indices_.IsValid()) device_->DestroyBuffer(indices_);
    if (vertices_.IsValid()) device_->DestroyBuffer(vertices_);
    if (pipeline_.IsValid()) device_->DestroyPipeline(pipeline_);
    if (layout_.IsValid()) device_->DestroyBindGroupLayout(layout_);
    if (vs_.IsValid()) device_->DestroyShader(vs_);
    if (fs_.IsValid()) device_->DestroyShader(fs_);
    indices_ = {};
    vertices_ = {};
    pipeline_ = {};
    layout_ = {};
    vs_ = {};
    fs_ = {};
    geometry_ready_ = false; // a renderer created again has fresh buffers, still in their upload state
    device_ = nullptr;
}

void CubeRenderer::Prepare(CommandList* list) {
    if (geometry_ready_) {
        return;
    }
    list->Barrier(vertices_, ResourceState::CopyDst, ResourceState::VertexBuffer);
    if (indexed_) {
        list->Barrier(indices_, ResourceState::CopyDst, ResourceState::IndexBuffer);
    }
}

bool CubeRenderer::Draw(CommandList* list, UniformRing* ring, const Mat4& mvp, u32 width, u32 height) {
    const UniformAllocation u = ring->Allocate(sizeof(Mat4));
    if (!u.Valid()) {
        return false;
    }
    std::memcpy(u.data, &mvp, sizeof(Mat4));
    BindGroupEntry entry;
    entry.binding = 0;
    entry.buffer = u.buffer;
    entry.offset = u.offset;
    entry.size = u.size;
    BindGroupDesc gd;
    gd.layout = layout_;
    gd.entries = Span<BindGroupEntry>(&entry, 1);
    gd.transient = true;
    const BindGroupHandle group = device_->CreateBindGroup(gd);
    if (!group.IsValid()) {
        return false;
    }
    list->SetPipeline(pipeline_);
    list->SetViewport(Viewport{0, 0, static_cast<f32>(width), static_cast<f32>(height), 0.0f, 1.0f});
    list->SetScissor(ScissorRect{0, 0, width, height});
    list->SetBindGroup(0, group);
    list->SetVertexBuffer(0, vertices_, 0);
    if (indexed_) {
        list->SetIndexBuffer(indices_, 0, IndexType::UInt16);
        list->DrawIndexed(kCubeIndexCount, 1, 0, 0, 0);
    } else {
        list->Draw(kCubeIndexCount, 1, 0, 0);
    }
    return true;
}

} // namespace demo
