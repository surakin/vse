#include "renderer/mesh.h"

#include <cmath>
#include <cstring>
#include <new>

#include "log/assert.h"
#include "math/math_util.h"
#include "renderer/renderer_log.h"

namespace engine_placeholder::renderer {

VertexLayoutData StandardVertexLayout() {
    VertexLayoutData layout;
    layout.attributes[0] = {0, 0, rhi::VertexFormat::Float3, 0};
    layout.attributes[1] = {1, 0, rhi::VertexFormat::Float3, 12};
    layout.attributes[2] = {2, 0, rhi::VertexFormat::Float4, 24};
    layout.attributes[3] = {3, 0, rhi::VertexFormat::Float2, 40};
    layout.attribute_count = 4;
    layout.stride = sizeof(StandardVertex);
    return layout;
}

// ---- MeshData ----

MeshData::~MeshData() { Release(); }

MeshData::MeshData(MeshData&& other) { *this = static_cast<MeshData&&>(other); }

MeshData& MeshData::operator=(MeshData&& other) {
    if (this != &other) {
        Release();
        layout_ = other.layout_;
        position_offset_ = other.position_offset_;
        vertices_ = other.vertices_;
        vertex_count_ = other.vertex_count_;
        indices_ = other.indices_;
        index_count_ = other.index_count_;
        index_type_ = other.index_type_;
        bounds_ = other.bounds_;
        other.vertices_ = nullptr;
        other.indices_ = nullptr;
        other.vertex_count_ = 0;
        other.index_count_ = 0;
    }
    return *this;
}

void MeshData::Release() {
    delete[] vertices_;
    delete[] indices_;
    vertices_ = nullptr;
    indices_ = nullptr;
    vertex_count_ = 0;
    index_count_ = 0;
    layout_ = VertexLayoutData();
    bounds_ = Bounds();
}

const char* MeshData::Set(const VertexLayoutData& layout, u32 position_offset, const void* vertices, u32 vertex_count,
                          rhi::IndexType index_type, const void* indices, u32 index_count) {
    Release();
    if (layout.stride == 0 || layout.stride > 512 || layout.attribute_count > rhi::kMaxVertexAttributes) {
        return "mesh: the vertex stride must be 1 to 512 bytes and the attribute count within the RHI limit";
    }
    if (layout.stride % 4 != 0 || position_offset % 4 != 0) {
        return "mesh: the vertex stride and the position offset must be multiples of 4 bytes";
    }
    if (static_cast<u64>(position_offset) + 12 > layout.stride) {
        return "mesh: the position does not fit inside the vertex";
    }
    if (vertices == nullptr || vertex_count == 0 || indices == nullptr || index_count == 0) {
        return "mesh: a mesh needs vertices and indices";
    }
    if (index_count % 3 != 0) {
        return "mesh: the index count must be a multiple of 3 (triangle list)";
    }
    if (index_type == rhi::IndexType::UInt16 && vertex_count > 65536) {
        return "mesh: 16-bit indices address at most 65536 vertices";
    }
    const u64 vertex_bytes = static_cast<u64>(vertex_count) * layout.stride;
    const u64 index_bytes = static_cast<u64>(index_count) * (index_type == rhi::IndexType::UInt16 ? 2 : 4);
    if (vertex_bytes > (1ull << 30) || index_bytes > (1ull << 30)) {
        return "mesh: more than 1 GiB of vertex or index data";
    }

    // Validate before copying: every index in range, every position finite.
    for (u32 i = 0; i < index_count; ++i) {
        u32 index;
        if (index_type == rhi::IndexType::UInt16) {
            u16 v;
            std::memcpy(&v, static_cast<const unsigned char*>(indices) + static_cast<usize>(i) * 2, 2);
            index = v;
        } else {
            std::memcpy(&index, static_cast<const unsigned char*>(indices) + static_cast<usize>(i) * 4, 4);
        }
        if (index >= vertex_count) {
            return "mesh: an index points past the last vertex";
        }
    }
    for (u32 v = 0; v < vertex_count; ++v) {
        f32 p[3];
        std::memcpy(p, static_cast<const unsigned char*>(vertices) + static_cast<usize>(v) * layout.stride + position_offset, sizeof(p));
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) {
            return "mesh: a vertex position is not finite";
        }
    }

    auto* v = new (std::nothrow) unsigned char[vertex_bytes];
    auto* ix = new (std::nothrow) unsigned char[index_bytes];
    if (v == nullptr || ix == nullptr) {
        delete[] v;
        delete[] ix;
        return "mesh: out of memory";
    }
    std::memcpy(v, vertices, vertex_bytes);
    std::memcpy(ix, indices, index_bytes);
    vertices_ = v;
    indices_ = ix;
    vertex_count_ = vertex_count;
    index_count_ = index_count;
    index_type_ = index_type;
    layout_ = layout;
    position_offset_ = position_offset;
    bounds_ = ComputeBounds(vertices_ + position_offset, vertex_count, layout.stride);
    return nullptr;
}

u32 MeshData::IndexAt(u32 i) const {
    ENGINE_ASSERT(i < index_count_, "MeshData::IndexAt: index out of range");
    if (index_type_ == rhi::IndexType::UInt16) {
        u16 v;
        std::memcpy(&v, indices_ + static_cast<usize>(i) * 2, 2);
        return v;
    }
    u32 v;
    std::memcpy(&v, indices_ + static_cast<usize>(i) * 4, 4);
    return v;
}

const f32* MeshData::PositionAt(u32 i) const {
    ENGINE_ASSERT(i < vertex_count_, "MeshData::PositionAt: vertex out of range");
    return reinterpret_cast<const f32*>(vertices_ + static_cast<usize>(i) * layout_.stride + position_offset_);
}

// ---- procedural shapes ----

namespace {

// Vertices and indices under construction, then handed to MeshData::Set.
struct Builder {
    StandardVertex* vertices = nullptr;
    u32* indices = nullptr;
    u32 vertex_count = 0;
    u32 index_count = 0;

    bool Allocate(u32 vertices_wanted, u32 indices_wanted) {
        vertices = new (std::nothrow) StandardVertex[vertices_wanted];
        indices = new (std::nothrow) u32[indices_wanted];
        vertex_count = vertices_wanted;
        index_count = 0;
        return vertices != nullptr && indices != nullptr;
    }
    ~Builder() {
        delete[] vertices;
        delete[] indices;
    }
    void Triangle(u32 a, u32 b, u32 c) {
        indices[index_count++] = a;
        indices[index_count++] = b;
        indices[index_count++] = c;
    }
    const char* Finish(MeshData* out) {
        const VertexLayoutData layout = StandardVertexLayout();
        if (vertex_count <= 65536) {
            u16* narrow = new (std::nothrow) u16[index_count];
            if (narrow == nullptr) {
                return "mesh: out of memory";
            }
            for (u32 i = 0; i < index_count; ++i) {
                narrow[i] = static_cast<u16>(indices[i]);
            }
            const char* error = out->Set(layout, 0, vertices, vertex_count, rhi::IndexType::UInt16, narrow, index_count);
            delete[] narrow;
            return error;
        }
        return out->Set(layout, 0, vertices, vertex_count, rhi::IndexType::UInt32, indices, index_count);
    }
};

void SetVertex(StandardVertex* v, const Vec3& position, const Vec3& normal, const Vec3& tangent, f32 handedness, f32 u, f32 w) {
    v->position[0] = position.x;
    v->position[1] = position.y;
    v->position[2] = position.z;
    v->normal[0] = normal.x;
    v->normal[1] = normal.y;
    v->normal[2] = normal.z;
    v->tangent[0] = tangent.x;
    v->tangent[1] = tangent.y;
    v->tangent[2] = tangent.z;
    v->tangent[3] = handedness;
    v->uv[0] = u;
    v->uv[1] = w;
}

bool Positive(f32 v) { return std::isfinite(v) && v > 0.0f; }

} // namespace

const char* MakeCubeMesh(f32 edge, MeshData* out) {
    ENGINE_ASSERT(out != nullptr, "MakeCubeMesh: out is null");
    if (!Positive(edge)) {
        return "cube: the edge length must be positive and finite";
    }
    // Per face: normal, and two axes u, v with u x v == normal so the corners below run counter-clockwise from outside.
    struct Face {
        Vec3 normal, u, v;
    };
    const Face faces[6] = {
        {Vec3(0, 0, 1), Vec3(1, 0, 0), Vec3(0, 1, 0)},  {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)},
        {Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)},  {Vec3(0, 0, -1), Vec3(0, 1, 0), Vec3(1, 0, 0)},
        {Vec3(-1, 0, 0), Vec3(0, 0, 1), Vec3(0, 1, 0)}, {Vec3(0, -1, 0), Vec3(1, 0, 0), Vec3(0, 0, 1)},
    };
    const f32 corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    const f32 uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    Builder b;
    if (!b.Allocate(24, 36)) {
        return "cube: out of memory";
    }
    for (u32 f = 0; f < 6; ++f) {
        for (u32 c = 0; c < 4; ++c) {
            const Vec3 p = (faces[f].normal + faces[f].u * corner[c][0] + faces[f].v * corner[c][1]) * (0.5f * edge);
            // The bitangent n x u equals v for this basis, so the handedness is +1.
            SetVertex(&b.vertices[f * 4 + c], p, faces[f].normal, faces[f].u, 1.0f, uv[c][0], uv[c][1]);
        }
        const u32 base = f * 4;
        b.Triangle(base, base + 1, base + 2);
        b.Triangle(base, base + 2, base + 3);
    }
    return b.Finish(out);
}

const char* MakeSphereMesh(f32 radius, u32 segments, u32 rings, MeshData* out) {
    ENGINE_ASSERT(out != nullptr, "MakeSphereMesh: out is null");
    if (!Positive(radius) || segments < 3 || rings < 2 || segments > 4096 || rings > 4096) {
        return "sphere: need a positive radius, 3 to 4096 segments and 2 to 4096 rings";
    }
    const u32 columns = segments + 1;
    const u32 vertex_total = (rings + 1) * columns;
    Builder b;
    if (!b.Allocate(vertex_total, segments * rings * 6)) {
        return "sphere: out of memory";
    }
    const f32 pi = 3.14159265358979f;
    for (u32 r = 0; r <= rings; ++r) {
        const f32 theta = pi * static_cast<f32>(r) / static_cast<f32>(rings); // from +Y down to -Y
        const f32 sin_t = Sin(theta);
        const f32 cos_t = Cos(theta);
        for (u32 s = 0; s <= segments; ++s) {
            const f32 phi = 2.0f * pi * static_cast<f32>(s) / static_cast<f32>(segments);
            const f32 sin_p = Sin(phi);
            const f32 cos_p = Cos(phi);
            const Vec3 normal(sin_t * cos_p, cos_t, sin_t * sin_p);
            // d(position)/d(phi) direction; it does not vanish at the poles, unlike the derivative itself. Seen from
            // outside, n x t points toward increasing theta, which is increasing V, so the handedness is +1.
            const Vec3 tangent(-sin_p, 0.0f, cos_p);
            SetVertex(&b.vertices[r * columns + s], normal * radius, normal, tangent, 1.0f, static_cast<f32>(s) / static_cast<f32>(segments),
                      static_cast<f32>(r) / static_cast<f32>(rings));
        }
    }
    for (u32 r = 0; r < rings; ++r) {
        for (u32 s = 0; s < segments; ++s) {
            const u32 v00 = r * columns + s;
            const u32 v01 = v00 + 1;
            const u32 v10 = v00 + columns;
            const u32 v11 = v10 + 1;
            // Counter-clockwise from outside: a step along +phi then +theta turns outward (d_phi x d_theta = normal).
            if (r != 0) {
                b.Triangle(v00, v01, v10); // skipped at the north pole, where v00 and v01 coincide
            }
            if (r != rings - 1) {
                b.Triangle(v01, v11, v10); // skipped at the south pole, where v10 and v11 coincide
            }
        }
    }
    return b.Finish(out);
}

const char* MakePlaneMesh(f32 width, f32 depth, u32 x_segments, u32 z_segments, MeshData* out) {
    ENGINE_ASSERT(out != nullptr, "MakePlaneMesh: out is null");
    if (!Positive(width) || !Positive(depth) || x_segments < 1 || z_segments < 1 || x_segments > 4096 || z_segments > 4096) {
        return "plane: need a positive size and 1 to 4096 segments per side";
    }
    const u32 columns = x_segments + 1;
    Builder b;
    if (!b.Allocate(columns * (z_segments + 1), x_segments * z_segments * 6)) {
        return "plane: out of memory";
    }
    for (u32 z = 0; z <= z_segments; ++z) {
        for (u32 x = 0; x <= x_segments; ++x) {
            const f32 fx = static_cast<f32>(x) / static_cast<f32>(x_segments);
            const f32 fz = static_cast<f32>(z) / static_cast<f32>(z_segments);
            // V grows along +Z, but cross(+Y, +X) is -Z, so the handedness is -1.
            SetVertex(&b.vertices[z * columns + x], Vec3((fx - 0.5f) * width, 0.0f, (fz - 0.5f) * depth), Vec3(0, 1, 0), Vec3(1, 0, 0), -1.0f, fx, fz);
        }
    }
    for (u32 z = 0; z < z_segments; ++z) {
        for (u32 x = 0; x < x_segments; ++x) {
            const u32 v00 = z * columns + x;
            const u32 v10 = v00 + 1;
            const u32 v01 = v00 + columns;
            const u32 v11 = v01 + 1;
            b.Triangle(v00, v01, v10); // counter-clockwise seen from +Y
            b.Triangle(v10, v01, v11);
        }
    }
    return b.Finish(out);
}

// ---- GpuMesh ----

GpuMesh::GpuMesh(GpuMesh&& other) { *this = static_cast<GpuMesh&&>(other); }

GpuMesh& GpuMesh::operator=(GpuMesh&& other) {
    if (this != &other) {
        Destroy();
        device_ = other.device_;
        vertex_buffer_ = other.vertex_buffer_;
        index_buffer_ = other.index_buffer_;
        vertex_count_ = other.vertex_count_;
        index_count_ = other.index_count_;
        index_type_ = other.index_type_;
        layout_ = other.layout_;
        bounds_ = other.bounds_;
        ready_ = other.ready_;
        other.device_ = nullptr;
        other.vertex_buffer_ = {};
        other.index_buffer_ = {};
        other.vertex_count_ = 0;
        other.index_count_ = 0;
        other.ready_ = false;
    }
    return *this;
}

bool GpuMesh::Create(rhi::Device* device, const MeshData& data, const char* debug_name) {
    ENGINE_ASSERT(device != nullptr, "GpuMesh::Create: device is null");
    ENGINE_ASSERT(!Valid(), "GpuMesh::Create: this mesh already holds buffers");
    ENGINE_ASSERT(data.Valid(), "GpuMesh::Create: the mesh data is empty");

    rhi::BufferDesc bd;
    bd.location = rhi::MemoryLocation::GpuOnly;
    bd.size = data.VertexBytes();
    bd.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst;
    bd.debug_name = debug_name;
    const rhi::BufferHandle vertex_buffer = device->CreateBuffer(bd);
    bd.size = data.IndexBytes();
    bd.usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst;
    const rhi::BufferHandle index_buffer = device->CreateBuffer(bd);

    bool ok = vertex_buffer.IsValid() && index_buffer.IsValid();
    if (ok) {
        ok = device->UploadBuffer(vertex_buffer, 0, data.Vertices(), data.VertexBytes()).ok &&
             device->UploadBuffer(index_buffer, 0, data.Indices(), data.IndexBytes()).ok;
    }
    if (!ok) {
        RenderLog(LogVerbosity::Error, "GpuMesh '%s': creating or uploading the buffers failed", debug_name != nullptr ? debug_name : "");
        if (vertex_buffer.IsValid()) device->DestroyBuffer(vertex_buffer);
        if (index_buffer.IsValid()) device->DestroyBuffer(index_buffer);
        return false;
    }
    device_ = device;
    vertex_buffer_ = vertex_buffer;
    index_buffer_ = index_buffer;
    vertex_count_ = data.VertexCount();
    index_count_ = data.IndexCount();
    index_type_ = data.IndexFormat();
    layout_ = data.Layout();
    bounds_ = data.GetBounds();
    ready_ = false;
    return true;
}

void GpuMesh::Destroy() {
    if (device_ == nullptr) {
        return;
    }
    if (index_buffer_.IsValid()) device_->DestroyBuffer(index_buffer_);
    if (vertex_buffer_.IsValid()) device_->DestroyBuffer(vertex_buffer_);
    index_buffer_ = {};
    vertex_buffer_ = {};
    vertex_count_ = 0;
    index_count_ = 0;
    ready_ = false;
    device_ = nullptr;
}

void GpuMesh::RecordPrepare(rhi::CommandList* list) {
    ENGINE_ASSERT(Valid() && list != nullptr, "GpuMesh::RecordPrepare: empty mesh or null list");
    if (ready_) {
        return;
    }
    list->Barrier(vertex_buffer_, rhi::ResourceState::CopyDst, rhi::ResourceState::VertexBuffer);
    list->Barrier(index_buffer_, rhi::ResourceState::CopyDst, rhi::ResourceState::IndexBuffer);
}

void GpuMesh::Draw(rhi::CommandList* list, u32 instance_count, u32 first_instance) const {
    ENGINE_ASSERT(Valid() && ready_, "GpuMesh::Draw: the mesh is empty or its prepare barriers were not submitted");
    list->SetVertexBuffer(0, vertex_buffer_, 0);
    list->SetIndexBuffer(index_buffer_, 0, index_type_);
    list->DrawIndexed(index_count_, instance_count, 0, 0, first_instance);
}

} // namespace engine_placeholder::renderer
