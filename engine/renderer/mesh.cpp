#include "renderer/mesh.h"

#include "lang/limits.h"

#include "sync/atomic.h"
#include "lang/scalar.h"

#include "cstr/format.h"
#include "lang/new.h"

#include "log/assert.h"
#include "math/math_util.h"
#include "renderer/renderer_log.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder::renderer
{

VertexLayoutData StandardVertexLayout()
{
    VertexLayoutData layout;
    layout.attributes[0] = {0, 0, rhi::VertexFormat::Float3, 0};
    layout.attributes[1] = {1, 0, rhi::VertexFormat::Float3, 12};
    layout.attributes[2] = {2, 0, rhi::VertexFormat::Float4, 24};
    layout.attributes[3] = {3, 0, rhi::VertexFormat::Float2, 40};
    layout.attribute_count = 4;
    layout.stride = sizeof(StandardVertex);
    return layout;
}

VertexLayoutData SkinnedVertexLayout(const VertexLayoutData& base)
{
    ENGINE_ASSERT(base.skin_stride == 0 && base.attribute_count + 2 <= rhi::kMaxVertexAttributes,
                  "SkinnedVertexLayout: the layout is already skinned or has no room for the skin attributes");
    VertexLayoutData layout = base;
    layout.attributes[layout.attribute_count++] = {kSkinJointsLocation, 1, rhi::VertexFormat::UInt8x4, 0};
    layout.attributes[layout.attribute_count++] = {kSkinWeightsLocation, 1, rhi::VertexFormat::UNorm8x4, 4};
    layout.skin_stride = kSkinVertexBytes;
    return layout;
}

VertexLayoutData ColorVertexLayout()
{
    VertexLayoutData layout = StandardVertexLayout();
    layout.attributes[4] = {4, 0, rhi::VertexFormat::UNorm8x4, 48};
    layout.attribute_count = 5;
    layout.stride = sizeof(ColorVertex);
    return layout;
}

// ---- MeshData ----

// A range of `count` items of `size` bytes starting at `offset` lies inside a region of `total` bytes.
static bool InRegion(u64 offset, u64 count, u64 size, u64 total)
{
    if (size != 0 && count > (Limits<u64>::Max() - offset) / size)
    {
        return false;
    }
    return offset + count * size <= total;
}

const char* MeshGeometry::Validate() const
{
    if (!bytes || vertex_count == 0 || index_count == 0 || layout.stride == 0 || layout.attribute_count == 0)
    {
        return "mesh: geometry needs a region of bytes, vertices, indices and a layout";
    }
    const u64 total = bytes->Bytes().Size();
    const bool skinned = layout.skin_stride != 0;
    if (!InRegion(vertex_offset, vertex_count, layout.stride, total) ||
        !InRegion(index_offset, index_count, index_type == rhi::IndexType::UInt16 ? 2 : 4, total) ||
        (skinned && !InRegion(skin_offset, vertex_count, kSkinVertexBytes, total)))
    {
        return "mesh: a section of the geometry lies outside its bytes";
    }
    u32 primitive = 3;
    bool strip = false;
    switch (topology)
    {
    case rhi::PrimitiveTopology::TriangleList: break;
    case rhi::PrimitiveTopology::LineList: primitive = 2; break;
    case rhi::PrimitiveTopology::TriangleStrip: strip = true; break;
    case rhi::PrimitiveTopology::LineStrip:
        primitive = 2;
        strip = true;
        break;
    case rhi::PrimitiveTopology::PointList: return "mesh: geometry has a topology the renderer does not draw";
    }
    if (strip ? index_count < primitive : index_count % primitive != 0)
    {
        return "mesh: the index count does not fit the topology";
    }
    if (skinned ? (skin_joint_count == 0 || skin_joint_count > 256 || layout.skin_stride != kSkinVertexBytes)
                : skin_joint_count != 0)
    {
        return "mesh: a skinned layout needs 1 to 256 joints and a stride of 8, and a rigid one no joints";
    }
    return nullptr;
}

// What an upload reads, as pointers that live for the call only.
namespace detail
{
struct MeshUploadView
{
    const void* vertices = nullptr;
    u64 vertex_bytes = 0;
    const void* indices = nullptr;
    u64 index_bytes = 0;
    const void* skin = nullptr;
    u64 skin_bytes = 0;
    bool skinned = false;
};
}  // namespace detail

namespace
{

using detail::MeshUploadView;

MeshUploadView ViewOfData(const MeshData& data)
{
    MeshUploadView v;
    v.vertices = data.Vertices();
    v.vertex_bytes = data.VertexBytes();
    v.indices = data.Indices();
    v.index_bytes = data.IndexBytes();
    v.skin = data.SkinBytes();
    v.skin_bytes = data.SkinByteCount();
    v.skinned = data.Skinned();
    return v;
}

MeshUploadView ViewOfGeometry(const MeshGeometry& g)
{
    MeshUploadView v;
    const byte* base = g.bytes->Bytes().Data();
    v.vertices = base + g.vertex_offset;
    v.vertex_bytes = static_cast<u64>(g.vertex_count) * g.layout.stride;
    v.indices = base + g.index_offset;
    v.index_bytes = static_cast<u64>(g.index_count) * (g.index_type == rhi::IndexType::UInt16 ? 2 : 4);
    v.skinned = g.layout.skin_stride != 0;
    v.skin = v.skinned ? base + g.skin_offset : nullptr;
    v.skin_bytes = v.skinned ? static_cast<u64>(g.vertex_count) * kSkinVertexBytes : 0;
    return v;
}

}  // namespace

MeshData::~MeshData()
{
    Release();
}

MeshData::MeshData(MeshData&& other) noexcept
{
    *this = static_cast<MeshData&&>(other);
}

MeshData& MeshData::operator=(MeshData&& other) noexcept
{
    if (this != &other)
    {
        Release();
        layout_ = other.layout_;
        position_offset_ = other.position_offset_;
        vertices_ = static_cast<Vector<byte>&&>(other.vertices_);
        skin_ = static_cast<Vector<byte>&&>(other.skin_);
        skin_joint_count_ = other.skin_joint_count_;
        vertex_count_ = other.vertex_count_;
        indices_ = static_cast<Vector<byte>&&>(other.indices_);
        index_count_ = other.index_count_;
        index_type_ = other.index_type_;
        topology_ = other.topology_;
        has_restart_ = other.has_restart_;
        bounds_ = other.bounds_;
        other.vertex_count_ = 0;
        other.index_count_ = 0;
    }
    return *this;
}

void MeshData::Release()
{
    vertices_.Reset();
    skin_.Reset();
    skin_joint_count_ = 0;
    indices_.Reset();
    vertex_count_ = 0;
    index_count_ = 0;
    topology_ = rhi::PrimitiveTopology::TriangleList;
    has_restart_ = false;
    layout_ = VertexLayoutData();
    bounds_ = Bounds();
}

namespace
{

// Whether `count` indices make whole primitives of `topology` (a strip needs at least one primitive).
bool IndexCountFits(rhi::PrimitiveTopology topology, u32 count)
{
    switch (topology)
    {
    case rhi::PrimitiveTopology::TriangleList: return count % 3 == 0;
    case rhi::PrimitiveTopology::TriangleStrip: return count >= 3;
    case rhi::PrimitiveTopology::LineList: return count % 2 == 0;
    case rhi::PrimitiveTopology::LineStrip: return count >= 2;
    case rhi::PrimitiveTopology::PointList: return count >= 1;
    }
    return false;
}

bool IsStrip(rhi::PrimitiveTopology topology)
{
    return topology == rhi::PrimitiveTopology::TriangleStrip || topology == rhi::PrimitiveTopology::LineStrip;
}

}  // namespace

const char* MeshData::Set(const VertexLayoutData& layout, u32 position_offset, const void* vertices, u32 vertex_count,
                          rhi::IndexType index_type, const void* indices, u32 index_count,
                          rhi::PrimitiveTopology topology)
{
    Release();
    if (layout.stride == 0 || layout.stride > 512 || layout.attribute_count > rhi::kMaxVertexAttributes)
    {
        return "mesh: the vertex stride must be 1 to 512 bytes and the attribute count within the RHI limit";
    }
    if (layout.stride % 4 != 0 || position_offset % 4 != 0)
    {
        return "mesh: the vertex stride and the position offset must be multiples of 4 bytes";
    }
    if (static_cast<u64>(position_offset) + 12 > layout.stride)
    {
        return "mesh: the position does not fit inside the vertex";
    }
    if (vertices == nullptr || vertex_count == 0 || indices == nullptr || index_count == 0)
    {
        return "mesh: a mesh needs vertices and indices";
    }
    if (static_cast<u32>(topology) > static_cast<u32>(rhi::PrimitiveTopology::PointList))
    {
        return "mesh: unknown primitive topology";
    }
    if (!IndexCountFits(topology, index_count))
    {
        return "mesh: the index count does not make whole primitives of the topology";
    }
    if (index_type == rhi::IndexType::UInt16 && vertex_count > (IsStrip(topology) ? 65535u : 65536u))
    {
        return IsStrip(topology)
                   ? "mesh: a strip with 16-bit indices addresses at most 65535 vertices (0xFFFF restarts the strip)"
                   : "mesh: 16-bit indices address at most 65536 vertices";
    }
    const u64 vertex_bytes = static_cast<u64>(vertex_count) * layout.stride;
    const u64 index_bytes = static_cast<u64>(index_count) * (index_type == rhi::IndexType::UInt16 ? 2 : 4);
    if (vertex_bytes > (1ull << 30) || index_bytes > (1ull << 30))
    {
        return "mesh: more than 1 GiB of vertex or index data";
    }

    // Validate before copying: every index in range (a strip's all-ones index is a restart marker), every position
    // finite.
    const bool strip = IsStrip(topology);
    bool has_restart = false;
    for (u32 i = 0; i < index_count; ++i)
    {
        u32 index;
        if (index_type == rhi::IndexType::UInt16)
        {
            u16 v;
            MemCopy(&v, static_cast<const byte*>(indices) + static_cast<usize>(i) * 2, 2);
            index = v;
        }
        else
        {
            MemCopy(&index, static_cast<const byte*>(indices) + static_cast<usize>(i) * 4, 4);
        }
        if (strip && index == (index_type == rhi::IndexType::UInt16 ? 0xFFFFu : 0xFFFFFFFFu))
        {
            has_restart = true;
            continue;
        }
        if (index >= vertex_count)
        {
            return "mesh: an index points past the last vertex";
        }
    }
    for (u32 v = 0; v < vertex_count; ++v)
    {
        f32 p[3];
        MemCopy(p, static_cast<const byte*>(vertices) + static_cast<usize>(v) * layout.stride + position_offset,
                sizeof(p));
        if (!IsFinite(p[0]) || !IsFinite(p[1]) || !IsFinite(p[2]))
        {
            return "mesh: a vertex position is not finite";
        }
    }

    Vector<byte> v;
    Vector<byte> ix;
    if (!v.TryResizeUninitialized(vertex_bytes) || !ix.TryResizeUninitialized(index_bytes))
    {
        return "mesh: out of memory";
    }
    MemCopy(v.Data(), vertices, vertex_bytes);
    MemCopy(ix.Data(), indices, index_bytes);
    vertices_ = static_cast<Vector<byte>&&>(v);
    indices_ = static_cast<Vector<byte>&&>(ix);
    vertex_count_ = vertex_count;
    index_count_ = index_count;
    index_type_ = index_type;
    layout_ = layout;
    position_offset_ = position_offset;
    topology_ = topology;
    has_restart_ = has_restart;
    bounds_ = ComputeBounds(vertices_.Data() + position_offset, vertex_count, layout.stride);
    return nullptr;
}

const char* MeshData::SetSkin(const void* skin, u32 joint_count)
{
    if (!Valid() || layout_.skin_stride != 0 || layout_.attribute_count + 2 > rhi::kMaxVertexAttributes)
    {
        return "mesh: the skin stream needs a valid rigid mesh with room for two more attributes";
    }
    if (skin == nullptr || joint_count == 0 || joint_count > 256)
    {
        return "mesh: a skin stream needs its bytes and 1 to 256 joints";
    }
    const byte* bytes = static_cast<const byte*>(skin);
    for (u32 v = 0; v < vertex_count_; ++v)
    {
        const byte* e = bytes + static_cast<usize>(v) * kSkinVertexBytes;
        u32 sum = 0;
        for (u32 k = 0; k < 4; ++k)
        {
            sum += static_cast<u32>(e[4 + k]);
            if (e[4 + k] != 0 && static_cast<u32>(e[k]) >= joint_count)
            {
                return "mesh: a skin weight refers to a joint the skeleton does not have";
            }
        }
        if (sum != 255)
        {
            return "mesh: the skin weights of a vertex must sum to exactly 255";
        }
    }
    Vector<byte> copy;
    if (!copy.TryResizeUninitialized(static_cast<usize>(vertex_count_) * kSkinVertexBytes))
    {
        return "mesh: out of memory";
    }
    MemCopy(copy.Data(), skin, copy.Size());
    skin_ = static_cast<Vector<byte>&&>(copy);
    skin_joint_count_ = joint_count;
    layout_ = SkinnedVertexLayout(layout_);
    return nullptr;
}

u32 MeshData::IndexAt(u32 i) const
{
    ENGINE_ASSERT(i < index_count_, "MeshData::IndexAt: index out of range");
    if (index_type_ == rhi::IndexType::UInt16)
    {
        u16 v;
        MemCopy(&v, indices_.Data() + static_cast<usize>(i) * 2, 2);
        return v;
    }
    u32 v;
    MemCopy(&v, indices_.Data() + static_cast<usize>(i) * 4, 4);
    return v;
}

const f32* MeshData::PositionAt(u32 i) const
{
    ENGINE_ASSERT(i < vertex_count_, "MeshData::PositionAt: vertex out of range");
    return reinterpret_cast<const f32*>(vertices_.Data() + static_cast<usize>(i) * layout_.stride + position_offset_);
}

// ---- procedural shapes ----

namespace
{

// Vertices and indices under construction, then handed to MeshData::Set.
struct Builder
{
    Vector<StandardVertex> vertices;
    Vector<u32> indices;
    u32 vertex_count = 0;
    u32 index_count = 0;

    bool Allocate(u32 vertices_wanted, u32 indices_wanted)
    {
        vertex_count = vertices_wanted;
        index_count = 0;
        return vertices.TryResize(vertices_wanted) && indices.TryResize(indices_wanted);
    }
    void Triangle(u32 a, u32 b, u32 c)
    {
        indices[index_count++] = a;
        indices[index_count++] = b;
        indices[index_count++] = c;
    }
    const char* Finish(MeshData* out)
    {
        const VertexLayoutData layout = StandardVertexLayout();
        if (vertex_count <= 65536)
        {
            Vector<u16> narrow;
            if (!narrow.TryResize(index_count))
            {
                return "mesh: out of memory";
            }
            for (u32 i = 0; i < index_count; ++i)
            {
                narrow[i] = static_cast<u16>(indices[i]);
            }
            return out->Set(layout, 0, vertices.Data(), vertex_count, rhi::IndexType::UInt16, narrow.Data(),
                            index_count);
        }
        return out->Set(layout, 0, vertices.Data(), vertex_count, rhi::IndexType::UInt32, indices.Data(), index_count);
    }
};

void SetVertex(StandardVertex* v, const Vec3& position, const Vec3& normal, const Vec3& tangent, f32 handedness, f32 u,
               f32 w)
{
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

bool Positive(f32 v)
{
    return IsFinite(v) && v > 0.0f;
}

}  // namespace

const char* MakeCubeMesh(f32 edge, MeshData* out)
{
    ENGINE_ASSERT(out != nullptr, "MakeCubeMesh: out is null");
    if (!Positive(edge))
    {
        return "cube: the edge length must be positive and finite";
    }
    // Per face: normal, and two axes u, v with u x v == normal so the corners below run counter-clockwise from outside.
    struct Face
    {
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
    if (!b.Allocate(24, 36))
    {
        return "cube: out of memory";
    }
    for (u32 f = 0; f < 6; ++f)
    {
        for (u32 c = 0; c < 4; ++c)
        {
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

const char* MakeSphereMesh(f32 radius, u32 segments, u32 rings, MeshData* out)
{
    ENGINE_ASSERT(out != nullptr, "MakeSphereMesh: out is null");
    if (!Positive(radius) || segments < 3 || rings < 2 || segments > 4096 || rings > 4096)
    {
        return "sphere: need a positive radius, 3 to 4096 segments and 2 to 4096 rings";
    }
    const u32 columns = segments + 1;
    const u32 vertex_total = (rings + 1) * columns;
    Builder b;
    if (!b.Allocate(vertex_total, segments * rings * 6))
    {
        return "sphere: out of memory";
    }
    const f32 pi = 3.14159265358979f;
    for (u32 r = 0; r <= rings; ++r)
    {
        const f32 theta = pi * static_cast<f32>(r) / static_cast<f32>(rings);  // from +Y down to -Y
        const f32 sin_t = Sin(theta);
        const f32 cos_t = Cos(theta);
        for (u32 s = 0; s <= segments; ++s)
        {
            const f32 phi = 2.0f * pi * static_cast<f32>(s) / static_cast<f32>(segments);
            const f32 sin_p = Sin(phi);
            const f32 cos_p = Cos(phi);
            const Vec3 normal(sin_t * cos_p, cos_t, sin_t * sin_p);
            // d(position)/d(phi) direction; it does not vanish at the poles, unlike the derivative itself. Seen from
            // outside, n x t points toward increasing theta, which is increasing V, so the handedness is +1.
            const Vec3 tangent(-sin_p, 0.0f, cos_p);
            SetVertex(&b.vertices[r * columns + s], normal * radius, normal, tangent, 1.0f,
                      static_cast<f32>(s) / static_cast<f32>(segments), static_cast<f32>(r) / static_cast<f32>(rings));
        }
    }
    for (u32 r = 0; r < rings; ++r)
    {
        for (u32 s = 0; s < segments; ++s)
        {
            const u32 v00 = r * columns + s;
            const u32 v01 = v00 + 1;
            const u32 v10 = v00 + columns;
            const u32 v11 = v10 + 1;
            // Counter-clockwise from outside: a step along +phi then +theta turns outward (d_phi x d_theta = normal).
            if (r != 0)
            {
                b.Triangle(v00, v01, v10);  // skipped at the north pole, where v00 and v01 coincide
            }
            if (r != rings - 1)
            {
                b.Triangle(v01, v11, v10);  // skipped at the south pole, where v10 and v11 coincide
            }
        }
    }
    return b.Finish(out);
}

const char* MakePlaneMesh(f32 width, f32 depth, u32 x_segments, u32 z_segments, MeshData* out)
{
    ENGINE_ASSERT(out != nullptr, "MakePlaneMesh: out is null");
    if (!Positive(width) || !Positive(depth) || x_segments < 1 || z_segments < 1 || x_segments > 4096 ||
        z_segments > 4096)
    {
        return "plane: need a positive size and 1 to 4096 segments per side";
    }
    const u32 columns = x_segments + 1;
    Builder b;
    if (!b.Allocate(columns * (z_segments + 1), x_segments * z_segments * 6))
    {
        return "plane: out of memory";
    }
    for (u32 z = 0; z <= z_segments; ++z)
    {
        for (u32 x = 0; x <= x_segments; ++x)
        {
            const f32 fx = static_cast<f32>(x) / static_cast<f32>(x_segments);
            const f32 fz = static_cast<f32>(z) / static_cast<f32>(z_segments);
            // V grows along +Z, but cross(+Y, +X) is -Z, so the handedness is -1.
            SetVertex(&b.vertices[z * columns + x], Vec3((fx - 0.5f) * width, 0.0f, (fz - 0.5f) * depth), Vec3(0, 1, 0),
                      Vec3(1, 0, 0), -1.0f, fx, fz);
        }
    }
    for (u32 z = 0; z < z_segments; ++z)
    {
        for (u32 x = 0; x < x_segments; ++x)
        {
            const u32 v00 = z * columns + x;
            const u32 v10 = v00 + 1;
            const u32 v01 = v00 + columns;
            const u32 v11 = v01 + 1;
            b.Triangle(v00, v01, v10);  // counter-clockwise seen from +Y
            b.Triangle(v10, v01, v11);
        }
    }
    return b.Finish(out);
}

const char* MakePlaneStripMesh(f32 width, f32 depth, u32 x_segments, u32 z_segments, MeshData* out)
{
    ENGINE_ASSERT(out != nullptr, "MakePlaneStripMesh: out is null");
    MeshData grid;
    if (const char* error = MakePlaneMesh(width, depth, x_segments, z_segments, &grid))
    {
        return error;
    }
    if (grid.VertexCount() > 65535)
    {
        return "plane: a strip grid addresses at most 65535 vertices";
    }
    const u32 columns = x_segments + 1;
    // One strip per row of cells: top0 bottom0 top1 bottom1 ... then the restart marker (except after the last row).
    const u32 per_row = 2 * columns + 1;
    const u32 count = per_row * z_segments - 1;
    Vector<u16> indices;
    if (!indices.TryResize(count))
    {
        return "plane: out of memory";
    }
    u32 n = 0;
    for (u32 z = 0; z < z_segments; ++z)
    {
        for (u32 x = 0; x < columns; ++x)
        {
            indices[n++] = static_cast<u16>(z * columns + x);
            indices[n++] = static_cast<u16>((z + 1) * columns + x);
        }
        if (z + 1 < z_segments)
        {
            indices[n++] = 0xFFFF;
        }
    }
    return out->Set(grid.Layout(), 0, grid.Vertices(), grid.VertexCount(), rhi::IndexType::UInt16, indices.Data(), n,
                    rhi::PrimitiveTopology::TriangleStrip);
}

const char* MakeColoredMesh(const MeshData& source, const u8* colors, u32 color_count, MeshData* out)
{
    ENGINE_ASSERT(out != nullptr && out != &source, "MakeColoredMesh: out is null or the source");
    if (!source.Valid())
    {
        return "colored mesh: the source mesh is empty";
    }
    const VertexLayoutData standard = StandardVertexLayout();
    const VertexLayoutData& have = source.Layout();
    bool same = have.stride == standard.stride && have.attribute_count == standard.attribute_count;
    for (u32 i = 0; same && i < standard.attribute_count; ++i)
    {
        same = have.attributes[i].location == standard.attributes[i].location &&
               have.attributes[i].format == standard.attributes[i].format &&
               have.attributes[i].offset == standard.attributes[i].offset;
    }
    if (!same)
    {
        return "colored mesh: the source mesh is not in the standard vertex layout";
    }
    if (colors == nullptr || color_count != source.VertexCount())
    {
        return "colored mesh: the number of colors must equal the number of vertices";
    }
    Vector<ColorVertex> vertices;
    if (!vertices.TryResize(source.VertexCount()))
    {
        return "colored mesh: out of memory";
    }
    const StandardVertex* in = static_cast<const StandardVertex*>(source.Vertices());
    for (u32 i = 0; i < source.VertexCount(); ++i)
    {
        vertices[i].base = in[i];
        for (u32 c = 0; c < 4; ++c)
        {
            vertices[i].color[c] = colors[i * 4 + c];
        }
    }
    return out->Set(ColorVertexLayout(), 0, vertices.Data(), source.VertexCount(), source.IndexFormat(),
                    source.Indices(), source.IndexCount(), source.Topology());
}

const char* MakeUniformColoredMesh(const MeshData& source, const u8 rgba[4], MeshData* out)
{
    ENGINE_ASSERT(out != nullptr && out != &source, "MakeUniformColoredMesh: out is null or the source");
    if (!source.Valid())
    {
        return "colored mesh: the source mesh is empty";
    }
    if (rgba == nullptr)
    {
        return "colored mesh: the color is null";
    }
    Vector<u8> colors;
    if (!colors.TryResize(static_cast<usize>(source.VertexCount()) * 4))
    {
        return "colored mesh: out of memory";
    }
    for (u32 i = 0; i < source.VertexCount(); ++i)
    {
        for (u32 c = 0; c < 4; ++c)
        {
            colors[i * 4 + c] = rgba[c];
        }
    }
    return MakeColoredMesh(source, colors.Data(), source.VertexCount(), out);
}

// ---- strips and lists ----

namespace
{

// A narrow or wide index array being built, handed to MeshData::Set with the source's vertices.
const char* SetIndices(const MeshData& source, const u32* indices, u32 count, rhi::PrimitiveTopology topology,
                       MeshData* out)
{
    const bool narrow = source.IndexFormat() == rhi::IndexType::UInt16;
    if (narrow)
    {
        Vector<u16> small;
        if (!small.TryResize(count))
        {
            return "mesh: out of memory";
        }
        for (u32 i = 0; i < count; ++i)
        {
            small[i] = static_cast<u16>(indices[i]);
        }
        return out->Set(source.Layout(), source.PositionOffset(), source.Vertices(), source.VertexCount(),
                        rhi::IndexType::UInt16, small.Data(), count, topology);
    }
    return out->Set(source.Layout(), source.PositionOffset(), source.Vertices(), source.VertexCount(),
                    rhi::IndexType::UInt32, indices, count, topology);
}

struct DirectedEdge
{
    u64 key;  // (from << 32) | to
    u32 triangle;
    u32 third;  // the triangle's third vertex
};

int CompareEdges(const void* a, const void* b)
{
    const u64 ka = static_cast<const DirectedEdge*>(a)->key;
    const u64 kb = static_cast<const DirectedEdge*>(b)->key;
    return ka < kb ? -1 : (ka > kb ? 1 : 0);
}

}  // namespace

const char* StripToList(const MeshData& strip, MeshData* out)
{
    ENGINE_ASSERT(out != nullptr && out != &strip, "StripToList: out is null or the source");
    if (!strip.Valid() || strip.Topology() != rhi::PrimitiveTopology::TriangleStrip)
    {
        return "strip to list: the mesh is not a triangle strip";
    }
    const u32 restart = strip.IndexFormat() == rhi::IndexType::UInt16 ? 0xFFFFu : 0xFFFFFFFFu;
    Vector<u32> list;
    if (!list.TryResize(static_cast<usize>(strip.IndexCount()) * 3))
    {
        return "strip to list: out of memory";
    }
    u32 n = 0;
    u32 position = 0;  // position inside the current strip
    u32 window[3] = {0, 0, 0};
    for (u32 i = 0; i < strip.IndexCount(); ++i)
    {
        const u32 index = strip.IndexAt(i);
        if (index == restart)
        {
            position = 0;
            continue;
        }
        window[position % 3] = index;
        ++position;
        if (position < 3)
        {
            continue;
        }
        const u32 k = position - 3;  // triangle number inside the strip
        const u32 a = window[k % 3];
        const u32 b = window[(k + 1) % 3];
        const u32 c = window[(k + 2) % 3];
        if (a == b || b == c || a == c)
        {
            continue;  // a degenerate join
        }
        if (k % 2 == 0)
        {
            list[n++] = a;
            list[n++] = b;
        }
        else
        {
            list[n++] = b;
            list[n++] = a;
        }
        list[n++] = c;
    }
    return n == 0 ? "strip to list: the strip holds no triangle"
                  : SetIndices(strip, list.Data(), n, rhi::PrimitiveTopology::TriangleList, out);
}

const char* ListToStrip(const MeshData& list, StripJoin join, MeshData* out)
{
    ENGINE_ASSERT(out != nullptr && out != &list, "ListToStrip: out is null or the source");
    if (!list.Valid() || list.Topology() != rhi::PrimitiveTopology::TriangleList)
    {
        return "list to strip: the mesh is not a triangle list";
    }
    const u32 triangles = list.IndexCount() / 3;
    Vector<DirectedEdge> edges_storage;
    Vector<bool> used_storage;
    // A lone triangle costs at most 3 indices plus a 3-index join.
    Vector<u32> result_storage;
    Vector<u32> strip_storage;
    if (!edges_storage.TryResize(static_cast<usize>(triangles) * 3) || !used_storage.TryResize(triangles) ||
        !result_storage.TryResize(static_cast<usize>(triangles) * 6 + 4) ||
        !strip_storage.TryResize(static_cast<usize>(triangles) + 2))
    {
        return "list to strip: out of memory";
    }
    DirectedEdge* edges = edges_storage.Data();
    bool* used = used_storage.Data();
    u32* result = result_storage.Data();
    u32* strip = strip_storage.Data();
    for (u32 t = 0; t < triangles; ++t)
    {
        used[t] = false;
        const u32 v[3] = {list.IndexAt(t * 3), list.IndexAt(t * 3 + 1), list.IndexAt(t * 3 + 2)};
        for (u32 e = 0; e < 3; ++e)
        {
            edges[t * 3 + e] = {(static_cast<u64>(v[e]) << 32) | v[(e + 1) % 3], t, v[(e + 2) % 3]};
        }
    }
    SortRaw(edges, static_cast<usize>(triangles) * 3, sizeof(DirectedEdge), &CompareEdges);

    auto find = [&](u32 from, u32 to) -> const DirectedEdge*
    {
        const u64 key = (static_cast<u64>(from) << 32) | to;
        usize lo = 0;
        usize hi = static_cast<usize>(triangles) * 3;
        while (lo < hi)
        {
            const usize mid = (lo + hi) / 2;
            if (edges[mid].key < key)
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        // Equal keys (a non-manifold mesh) are tried in order until an unused triangle is found.
        for (; lo < static_cast<usize>(triangles) * 3 && edges[lo].key == key; ++lo)
        {
            if (!used[edges[lo].triangle])
            {
                return &edges[lo];
            }
        }
        return nullptr;
    };

    u32 n = 0;
    // A list that used all 65536 16-bit values comes out in 32-bit indices (see below), so the marker has the output's
    // width.
    const bool widen = list.IndexFormat() == rhi::IndexType::UInt16 && list.VertexCount() > 65535;
    const u32 restart = (list.IndexFormat() == rhi::IndexType::UInt16 && !widen) ? 0xFFFFu : 0xFFFFFFFFu;
    for (u32 t = 0; t < triangles; ++t)
    {
        if (used[t])
        {
            continue;
        }
        used[t] = true;
        u32 length = 0;
        strip[length++] = list.IndexAt(t * 3);
        strip[length++] = list.IndexAt(t * 3 + 1);
        strip[length++] = list.IndexAt(t * 3 + 2);
        for (;;)
        {
            // The next triangle (number length - 2) is drawn as (p, q, r) when that number is even and (q, p, r) when
            // odd.
            const u32 p = strip[length - 2];
            const u32 q = strip[length - 1];
            const u32 number = length - 2;
            const DirectedEdge* next = (number % 2 == 0) ? find(p, q) : find(q, p);
            if (next == nullptr)
            {
                break;
            }
            used[next->triangle] = true;
            strip[length++] = next->third;
        }
        if (n != 0)
        {
            if (join == StripJoin::Restart)
            {
                result[n++] = restart;
            }
            else
            {
                const u32 last = result[n - 1];
                result[n++] = last;
                result[n++] = strip[0];
                if (n % 2 != 0)
                {
                    result[n++] = strip[0];  // the next real triangle must start at an even position
                }
            }
        }
        for (u32 i = 0; i < length; ++i)
        {
            result[n++] = strip[i];
        }
    }
    const char* error = nullptr;
    if (widen)
    {
        // The list used all 65536 16-bit values; a strip cannot (0xFFFF restarts), so the result uses 32-bit indices.
        error = out->Set(list.Layout(), list.PositionOffset(), list.Vertices(), list.VertexCount(),
                         rhi::IndexType::UInt32, result, n, rhi::PrimitiveTopology::TriangleStrip);
    }
    else
    {
        error = SetIndices(list, result, n, rhi::PrimitiveTopology::TriangleStrip, out);
    }
    return error;
}

// ---- GpuMesh ----

GpuMesh::GpuMesh(GpuMesh&& other) noexcept
{
    MoveFrom(other);
}

GpuMesh& GpuMesh::operator=(GpuMesh&& other) noexcept
{
    if (this != &other)
    {
        Destroy();
        MoveFrom(other);
    }
    return *this;
}

void GpuMesh::MoveFrom(GpuMesh& other)
{
    owned_ = static_cast<MeshData&&>(other.owned_);
    was_ready_ = other.was_ready_;
    other.was_ready_ = false;
    geometry_ = static_cast<MeshGeometry&&>(other.geometry_);
    has_geometry_ = other.has_geometry_;
    other.geometry_ = MeshGeometry{};
    other.has_geometry_ = false;
    source_ = other.source_;
    if (source_.borrowed == &other.owned_)
    {
        source_.borrowed = &owned_;
    }
    MemCopy(name_, other.name_, sizeof(name_));
    recreatable_ = other.recreatable_;
    registration_.Adopt(&other.registration_, this);
    device_ = other.device_;
    vertex_buffer_ = other.vertex_buffer_;
    index_buffer_ = other.index_buffer_;
    skin_buffer_ = other.skin_buffer_;
    vertex_count_ = other.vertex_count_;
    index_count_ = other.index_count_;
    index_type_ = other.index_type_;
    topology_ = other.topology_;
    has_restart_ = other.has_restart_;
    layout_ = other.layout_;
    skin_joint_count_ = other.skin_joint_count_;
    bounds_ = other.bounds_;
    sort_id_ = other.sort_id_;
    ready_ = other.ready_;
    prepared_ = other.prepared_;
    other.source_ = Source{};
    other.recreatable_ = false;
    other.sort_id_ = 0;
    other.device_ = nullptr;
    other.vertex_buffer_ = {};
    other.skin_buffer_ = {};
    other.index_buffer_ = {};
    other.vertex_count_ = 0;
    other.index_count_ = 0;
    other.ready_ = false;
    other.prepared_ = false;
}

bool GpuMesh::Upload(rhi::Device* device, const detail::MeshUploadView& data, const char* debug_name,
                     rhi::BufferHandle* vb, rhi::BufferHandle* ib, rhi::BufferHandle* sb)
{
    rhi::BufferDesc bd;
    bd.location = rhi::MemoryLocation::GpuOnly;
    const u64 vertex_bytes = data.vertex_bytes;
    const u64 index_bytes = data.index_bytes;
    const bool skinned = data.skinned;
    const u64 skin_bytes = data.skin_bytes;
    bd.size = vertex_bytes;
    bd.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst;
    bd.debug_name = debug_name;
    const rhi::BufferHandle vertex_buffer = device->CreateBuffer(bd);
    bd.size = index_bytes;
    bd.usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst;
    const rhi::BufferHandle index_buffer = device->CreateBuffer(bd);

    rhi::BufferHandle skin_buffer;
    if (skinned)
    {
        bd.size = skin_bytes;
        bd.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst;
        skin_buffer = device->CreateBuffer(bd);
    }
    bool ok = vertex_buffer.IsValid() && index_buffer.IsValid() && (!skinned || skin_buffer.IsValid());
    if (ok)
    {
        ok = device->UploadBuffer(vertex_buffer, 0, data.vertices, vertex_bytes).ok &&
             device->UploadBuffer(index_buffer, 0, data.indices, index_bytes).ok &&
             (!skinned || device->UploadBuffer(skin_buffer, 0, data.skin, skin_bytes).ok);
    }
    if (!ok)
    {
        RenderLog(LogVerbosity::Error, "GpuMesh '%s': creating or uploading the buffers failed",
                  debug_name != nullptr ? debug_name : "");
        if (vertex_buffer.IsValid())
        {
            device->DestroyBuffer(vertex_buffer);
        }
        if (index_buffer.IsValid())
        {
            device->DestroyBuffer(index_buffer);
        }
        if (skin_buffer.IsValid())
        {
            device->DestroyBuffer(skin_buffer);
        }
        return false;
    }
    *vb = vertex_buffer;
    *ib = index_buffer;
    *sb = skin_buffer;
    return true;
}

namespace
{

// One counter for every way a mesh is made: ids differ between meshes created one after another (1 to 2^24 - 1).
u32 NextSortId()
{
    static Atomic<u32> next_sort_id{1};
    return next_sort_id.FetchAdd(1, MemoryOrder::Relaxed) % 0xffffffu + 1;
}

}  // namespace

bool GpuMesh::Create(rhi::Device* device, const MeshData& data, const char* debug_name)
{
    ENGINE_ASSERT(device != nullptr, "GpuMesh::Create: device is null");
    ENGINE_ASSERT(!Valid(), "GpuMesh::Create: this mesh already holds buffers");
    ENGINE_ASSERT(data.Valid(), "GpuMesh::Create: the mesh data is empty");

    rhi::BufferHandle vertex_buffer;
    rhi::BufferHandle index_buffer;
    rhi::BufferHandle skin_buffer;
    if (!Upload(device, ViewOfData(data), debug_name, &vertex_buffer, &index_buffer, &skin_buffer))
    {
        return false;
    }
    device_ = device;
    vertex_buffer_ = vertex_buffer;
    index_buffer_ = index_buffer;
    skin_buffer_ = skin_buffer;
    vertex_count_ = data.VertexCount();
    index_count_ = data.IndexCount();
    index_type_ = data.IndexFormat();
    topology_ = data.Topology();
    has_restart_ = data.HasPrimitiveRestart();
    layout_ = data.Layout();
    skin_joint_count_ = data.SkinJointCount();
    bounds_ = data.GetBounds();
    sort_id_ = NextSortId();
    ready_ = false;
    prepared_ = false;
    return true;
}

bool GpuMesh::CreateFromGeometry(rhi::Device* device, const MeshGeometry& geometry, DeviceResources* resources,
                                 const char* debug_name)
{
    ENGINE_ASSERT(device != nullptr, "GpuMesh::CreateFromGeometry: device is null");
    ENGINE_ASSERT(!Valid(), "GpuMesh::CreateFromGeometry: this mesh already holds buffers");
    if (const char* refused = geometry.Validate())
    {
        RenderLog(LogVerbosity::Error, "GpuMesh '%s': %s", debug_name != nullptr ? debug_name : "", refused);
        return false;
    }
    rhi::BufferHandle vertex_buffer;
    rhi::BufferHandle index_buffer;
    rhi::BufferHandle skin_buffer;
    if (!Upload(device, ViewOfGeometry(geometry), debug_name, &vertex_buffer, &index_buffer, &skin_buffer))
    {
        return false;
    }
    device_ = device;
    vertex_buffer_ = vertex_buffer;
    index_buffer_ = index_buffer;
    skin_buffer_ = skin_buffer;
    vertex_count_ = geometry.vertex_count;
    index_count_ = geometry.index_count;
    index_type_ = geometry.index_type;
    topology_ = geometry.topology;
    layout_ = geometry.layout;
    skin_joint_count_ = geometry.skin_joint_count;
    bounds_ = geometry.bounds;
    has_restart_ = false;
    if (topology_ == rhi::PrimitiveTopology::TriangleStrip || topology_ == rhi::PrimitiveTopology::LineStrip)
    {
        const byte* raw = geometry.bytes->Bytes().Data() + geometry.index_offset;
        for (u32 i = 0; i < index_count_ && !has_restart_; ++i)
        {
            if (index_type_ == rhi::IndexType::UInt16)
            {
                u16 v;
                MemCopy(&v, raw + static_cast<usize>(i) * 2, 2);
                has_restart_ = v == 0xFFFFu;
            }
            else
            {
                u32 v;
                MemCopy(&v, raw + static_cast<usize>(i) * 4, 4);
                has_restart_ = v == 0xFFFFFFFFu;
            }
        }
    }
    sort_id_ = NextSortId();
    ready_ = false;
    prepared_ = false;
    geometry_ = geometry;
    has_geometry_ = true;
    FormatTo(name_, sizeof(name_), "%s", debug_name != nullptr ? debug_name : "mesh");
    recreatable_ = true;
    if (!registration_.Attach(resources, this, tiers::kMeshes))
    {
        // Nothing stays allocated: the buffers, the source and the share of the bytes go.
        Release();
        recreatable_ = false;
        has_geometry_ = false;
        geometry_ = MeshGeometry{};
        vertex_count_ = 0;
        index_count_ = 0;
        sort_id_ = 0;
        return false;
    }
    return true;
}

bool GpuMesh::MakeRecreatable(DeviceResources* resources, const Source& source, const char* debug_name)
{
    ENGINE_ASSERT(Valid(), "GpuMesh::MakeRecreatable: the mesh is empty");
    ENGINE_ASSERT(!has_geometry_, "GpuMesh::MakeRecreatable: the mesh already has its geometry as its source");
    ENGINE_ASSERT(source.borrowed != nullptr || source.load != nullptr, "GpuMesh::MakeRecreatable: no source");
    source_ = source;
    FormatTo(name_, sizeof(name_), "%s", debug_name != nullptr ? debug_name : "mesh");
    recreatable_ = true;
    if (!registration_.Attach(resources, this, tiers::kMeshes))
    {
        recreatable_ = false;
        source_ = Source{};
        return false;
    }
    return true;
}

bool GpuMesh::AdoptSource(DeviceResources* resources, MeshData&& data, const char* debug_name)
{
    ENGINE_ASSERT(Valid() && data.Valid(), "GpuMesh::AdoptSource: the mesh or the data is empty");
    if (resources == nullptr)
    {
        return true;  // nothing could use the data
    }
    owned_ = static_cast<MeshData&&>(data);
    Source source;
    source.borrowed = &owned_;
    return MakeRecreatable(resources, source, debug_name);
}

void GpuMesh::Release()
{
    if (device_ == nullptr)
    {
        return;
    }
    if (skin_buffer_.IsValid())
    {
        device_->DestroyBuffer(skin_buffer_);
    }
    if (index_buffer_.IsValid())
    {
        device_->DestroyBuffer(index_buffer_);
    }
    if (vertex_buffer_.IsValid())
    {
        device_->DestroyBuffer(vertex_buffer_);
    }
    skin_buffer_ = {};
    index_buffer_ = {};
    vertex_buffer_ = {};
    was_ready_ = was_ready_ || ready_;
    ready_ = false;
    prepared_ = false;
    device_ = nullptr;
}

bool GpuMesh::Recreate(rhi::Device* device)
{
    ENGINE_ASSERT(!Valid(), "GpuMesh::Recreate: the mesh still holds buffers");
    if (!recreatable_)
    {
        RenderLog(LogVerbosity::Error, "GpuMesh '%s': no source to recreate it from", name_);
        return false;
    }
    MeshData loaded;
    detail::MeshUploadView view;
    u32 vertices = 0;
    u32 indices = 0;
    u32 joints = 0;
    bool skinned = false;
    if (has_geometry_)
    {
        if (const char* why = geometry_.Validate())
        {
            RenderLog(LogVerbosity::Error, "GpuMesh '%s': %s", name_, why);
            return false;
        }
        view = ViewOfGeometry(geometry_);
        vertices = geometry_.vertex_count;
        indices = geometry_.index_count;
        joints = geometry_.skin_joint_count;
        skinned = geometry_.layout.skin_stride != 0;
    }
    else
    {
        const MeshData* data = source_.borrowed;
        if (data == nullptr)
        {
            if (!source_.load(source_.user, &loaded) || !loaded.Valid())
            {
                RenderLog(LogVerbosity::Error, "GpuMesh '%s': the loader failed", name_);
                return false;
            }
            data = &loaded;
        }
        view = ViewOfData(*data);
        vertices = data->VertexCount();
        indices = data->IndexCount();
        joints = data->SkinJointCount();
        skinned = data->Skinned();
        if (!data->Valid())
        {
            vertices = 0;
        }
    }
    if (vertices == 0 || vertices != vertex_count_ || indices != index_count_ ||
        skinned != (layout_.skin_stride != 0) || joints != skin_joint_count_)
    {
        RenderLog(LogVerbosity::Error, "GpuMesh '%s': the source no longer matches the mesh", name_);
        return false;
    }
    if (!Upload(device, view, name_, &vertex_buffer_, &index_buffer_, &skin_buffer_))
    {
        return false;
    }
    device_ = device;
    if (was_ready_)
    {
        // Put the buffers back into their read states, as before the reset.
        const bool own_frame = !device->InFrame();  // inside an open frame the work joins it
        if (own_frame)
        {
            device->BeginFrame();
        }
        rhi::CommandList* list = device->CreateCommandList();
        bool submitted = false;
        rhi::FenceValue fence = 0;
        if (list != nullptr)
        {
            list->Begin();
            RecordPrepare(list);
            list->End();
            rhi::CommandList* lists[] = {list};
            fence = device->Submit(lists, 1);
            submitted = fence != 0;
        }
        if (own_frame)
        {
            device->EndFrame();
        }
        if (own_frame)
        {
            device->WaitIdle();
        }
        else if (submitted)
        {
            device->WaitFence(fence, ~0ull);
        }
        if (!submitted)
        {
            RenderLog(LogVerbosity::Error, "GpuMesh '%s': preparing the rebuilt buffers failed", name_);
            Release();
            return false;
        }
        MarkReady();
    }
    return true;
}

void GpuMesh::Destroy()
{
    registration_.Reset();
    Release();
    owned_ = MeshData{};
    source_ = Source{};
    geometry_ = MeshGeometry{};
    has_geometry_ = false;
    recreatable_ = false;
    was_ready_ = false;
    vertex_count_ = 0;
    index_count_ = 0;
    sort_id_ = 0;
}

void GpuMesh::RecordPrepare(rhi::CommandList* list)
{
    ENGINE_ASSERT(Valid() && list != nullptr, "GpuMesh::RecordPrepare: empty mesh or null list");
    if (ready_)
    {
        return;
    }
    list->Barrier(vertex_buffer_, rhi::ResourceState::CopyDst, rhi::ResourceState::VertexBuffer);
    list->Barrier(index_buffer_, rhi::ResourceState::CopyDst, rhi::ResourceState::IndexBuffer);
    if (skin_buffer_.IsValid())
    {
        list->Barrier(skin_buffer_, rhi::ResourceState::CopyDst, rhi::ResourceState::VertexBuffer);
    }
    prepared_ = true;
}

void GpuMesh::Draw(rhi::CommandList* list, u32 instance_count, u32 first_instance) const
{
    ENGINE_ASSERT(Valid() && (prepared_ || ready_),
                  "GpuMesh::Draw: the mesh is empty or RecordPrepare was never called");
    list->SetVertexBuffer(0, vertex_buffer_, 0);
    if (skin_buffer_.IsValid())
    {
        list->SetVertexBuffer(1, skin_buffer_, 0);
    }
    list->SetIndexBuffer(index_buffer_, 0, index_type_);
    list->DrawIndexed(index_count_, instance_count, 0, 0, first_instance);
}

}  // namespace engine_placeholder::renderer
