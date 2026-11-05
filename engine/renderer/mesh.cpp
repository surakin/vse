#include "renderer/mesh.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
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

VertexLayoutData ColorVertexLayout() {
    VertexLayoutData layout = StandardVertexLayout();
    layout.attributes[4] = {4, 0, rhi::VertexFormat::UNorm8x4, 48};
    layout.attribute_count = 5;
    layout.stride = sizeof(ColorVertex);
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
        vertices_ = static_cast<Vector<unsigned char>&&>(other.vertices_);
        vertex_count_ = other.vertex_count_;
        indices_ = static_cast<Vector<unsigned char>&&>(other.indices_);
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

void MeshData::Release() {
    vertices_.Reset();
    indices_.Reset();
    vertex_count_ = 0;
    index_count_ = 0;
    topology_ = rhi::PrimitiveTopology::TriangleList;
    has_restart_ = false;
    layout_ = VertexLayoutData();
    bounds_ = Bounds();
}

namespace {

// Whether `count` indices make whole primitives of `topology` (a strip needs at least one primitive).
bool IndexCountFits(rhi::PrimitiveTopology topology, u32 count) {
    switch (topology) {
    case rhi::PrimitiveTopology::TriangleList: return count % 3 == 0;
    case rhi::PrimitiveTopology::TriangleStrip: return count >= 3;
    case rhi::PrimitiveTopology::LineList: return count % 2 == 0;
    case rhi::PrimitiveTopology::LineStrip: return count >= 2;
    case rhi::PrimitiveTopology::PointList: return count >= 1;
    }
    return false;
}

bool IsStrip(rhi::PrimitiveTopology topology) {
    return topology == rhi::PrimitiveTopology::TriangleStrip || topology == rhi::PrimitiveTopology::LineStrip;
}

} // namespace

const char* MeshData::Set(const VertexLayoutData& layout, u32 position_offset, const void* vertices, u32 vertex_count,
                          rhi::IndexType index_type, const void* indices, u32 index_count, rhi::PrimitiveTopology topology) {
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
    if (static_cast<u32>(topology) > static_cast<u32>(rhi::PrimitiveTopology::PointList)) {
        return "mesh: unknown primitive topology";
    }
    if (!IndexCountFits(topology, index_count)) {
        return "mesh: the index count does not make whole primitives of the topology";
    }
    if (index_type == rhi::IndexType::UInt16 && vertex_count > (IsStrip(topology) ? 65535u : 65536u)) {
        return IsStrip(topology) ? "mesh: a strip with 16-bit indices addresses at most 65535 vertices (0xFFFF restarts the strip)"
                                 : "mesh: 16-bit indices address at most 65536 vertices";
    }
    const u64 vertex_bytes = static_cast<u64>(vertex_count) * layout.stride;
    const u64 index_bytes = static_cast<u64>(index_count) * (index_type == rhi::IndexType::UInt16 ? 2 : 4);
    if (vertex_bytes > (1ull << 30) || index_bytes > (1ull << 30)) {
        return "mesh: more than 1 GiB of vertex or index data";
    }

    // Validate before copying: every index in range (a strip's all-ones index is a restart marker), every position finite.
    const bool strip = IsStrip(topology);
    bool has_restart = false;
    for (u32 i = 0; i < index_count; ++i) {
        u32 index;
        if (index_type == rhi::IndexType::UInt16) {
            u16 v;
            std::memcpy(&v, static_cast<const unsigned char*>(indices) + static_cast<usize>(i) * 2, 2);
            index = v;
        } else {
            std::memcpy(&index, static_cast<const unsigned char*>(indices) + static_cast<usize>(i) * 4, 4);
        }
        if (strip && index == (index_type == rhi::IndexType::UInt16 ? 0xFFFFu : 0xFFFFFFFFu)) {
            has_restart = true;
            continue;
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

    Vector<unsigned char> v;
    Vector<unsigned char> ix;
    if (!v.TryResizeUninitialized(vertex_bytes) || !ix.TryResizeUninitialized(index_bytes)) {
        return "mesh: out of memory";
    }
    std::memcpy(v.Data(), vertices, vertex_bytes);
    std::memcpy(ix.Data(), indices, index_bytes);
    vertices_ = static_cast<Vector<unsigned char>&&>(v);
    indices_ = static_cast<Vector<unsigned char>&&>(ix);
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

u32 MeshData::IndexAt(u32 i) const {
    ENGINE_ASSERT(i < index_count_, "MeshData::IndexAt: index out of range");
    if (index_type_ == rhi::IndexType::UInt16) {
        u16 v;
        std::memcpy(&v, indices_.Data() + static_cast<usize>(i) * 2, 2);
        return v;
    }
    u32 v;
    std::memcpy(&v, indices_.Data() + static_cast<usize>(i) * 4, 4);
    return v;
}

const f32* MeshData::PositionAt(u32 i) const {
    ENGINE_ASSERT(i < vertex_count_, "MeshData::PositionAt: vertex out of range");
    return reinterpret_cast<const f32*>(vertices_.Data() + static_cast<usize>(i) * layout_.stride + position_offset_);
}

// ---- procedural shapes ----

namespace {

// Vertices and indices under construction, then handed to MeshData::Set.
struct Builder {
    Vector<StandardVertex> vertices;
    Vector<u32> indices;
    u32 vertex_count = 0;
    u32 index_count = 0;

    bool Allocate(u32 vertices_wanted, u32 indices_wanted) {
        vertex_count = vertices_wanted;
        index_count = 0;
        return vertices.TryResize(vertices_wanted) && indices.TryResize(indices_wanted);
    }
    void Triangle(u32 a, u32 b, u32 c) {
        indices[index_count++] = a;
        indices[index_count++] = b;
        indices[index_count++] = c;
    }
    const char* Finish(MeshData* out) {
        const VertexLayoutData layout = StandardVertexLayout();
        if (vertex_count <= 65536) {
            Vector<u16> narrow;
            if (!narrow.TryResize(index_count)) {
                return "mesh: out of memory";
            }
            for (u32 i = 0; i < index_count; ++i) {
                narrow[i] = static_cast<u16>(indices[i]);
            }
            return out->Set(layout, 0, vertices.Data(), vertex_count, rhi::IndexType::UInt16, narrow.Data(), index_count);
        }
        return out->Set(layout, 0, vertices.Data(), vertex_count, rhi::IndexType::UInt32, indices.Data(), index_count);
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

const char* MakePlaneStripMesh(f32 width, f32 depth, u32 x_segments, u32 z_segments, MeshData* out) {
    ENGINE_ASSERT(out != nullptr, "MakePlaneStripMesh: out is null");
    MeshData grid;
    if (const char* error = MakePlaneMesh(width, depth, x_segments, z_segments, &grid)) return error;
    if (grid.VertexCount() > 65535) return "plane: a strip grid addresses at most 65535 vertices";
    const u32 columns = x_segments + 1;
    // One strip per row of cells: top0 bottom0 top1 bottom1 ... then the restart marker (except after the last row).
    const u32 per_row = 2 * columns + 1;
    const u32 count = per_row * z_segments - 1;
    Vector<u16> indices;
    if (!indices.TryResize(count)) return "plane: out of memory";
    u32 n = 0;
    for (u32 z = 0; z < z_segments; ++z) {
        for (u32 x = 0; x < columns; ++x) {
            indices[n++] = static_cast<u16>(z * columns + x);
            indices[n++] = static_cast<u16>((z + 1) * columns + x);
        }
        if (z + 1 < z_segments) indices[n++] = 0xFFFF;
    }
    return out->Set(grid.Layout(), 0, grid.Vertices(), grid.VertexCount(), rhi::IndexType::UInt16, indices.Data(), n, rhi::PrimitiveTopology::TriangleStrip);
}

const char* MakeColoredMesh(const MeshData& source, const u8* colors, u32 color_count, MeshData* out) {
    ENGINE_ASSERT(out != nullptr && out != &source, "MakeColoredMesh: out is null or the source");
    if (!source.Valid()) return "colored mesh: the source mesh is empty";
    const VertexLayoutData standard = StandardVertexLayout();
    const VertexLayoutData& have = source.Layout();
    bool same = have.stride == standard.stride && have.attribute_count == standard.attribute_count;
    for (u32 i = 0; same && i < standard.attribute_count; ++i) {
        same = have.attributes[i].location == standard.attributes[i].location && have.attributes[i].format == standard.attributes[i].format &&
               have.attributes[i].offset == standard.attributes[i].offset;
    }
    if (!same) return "colored mesh: the source mesh is not in the standard vertex layout";
    if (colors == nullptr || color_count != source.VertexCount()) return "colored mesh: the number of colors must equal the number of vertices";
    Vector<ColorVertex> vertices;
    if (!vertices.TryResize(source.VertexCount())) return "colored mesh: out of memory";
    const StandardVertex* in = static_cast<const StandardVertex*>(source.Vertices());
    for (u32 i = 0; i < source.VertexCount(); ++i) {
        vertices[i].base = in[i];
        for (u32 c = 0; c < 4; ++c) vertices[i].color[c] = colors[i * 4 + c];
    }
    return out->Set(ColorVertexLayout(), 0, vertices.Data(), source.VertexCount(), source.IndexFormat(), source.Indices(), source.IndexCount(), source.Topology());
}

const char* MakeUniformColoredMesh(const MeshData& source, const u8 rgba[4], MeshData* out) {
    ENGINE_ASSERT(out != nullptr && out != &source, "MakeUniformColoredMesh: out is null or the source");
    if (!source.Valid()) return "colored mesh: the source mesh is empty";
    if (rgba == nullptr) return "colored mesh: the color is null";
    Vector<u8> colors;
    if (!colors.TryResize(static_cast<usize>(source.VertexCount()) * 4)) return "colored mesh: out of memory";
    for (u32 i = 0; i < source.VertexCount(); ++i) {
        for (u32 c = 0; c < 4; ++c) colors[i * 4 + c] = rgba[c];
    }
    return MakeColoredMesh(source, colors.Data(), source.VertexCount(), out);
}

// ---- strips and lists ----

namespace {

// A narrow or wide index array being built, handed to MeshData::Set with the source's vertices.
const char* SetIndices(const MeshData& source, const u32* indices, u32 count, rhi::PrimitiveTopology topology, MeshData* out) {
    const bool narrow = source.IndexFormat() == rhi::IndexType::UInt16;
    if (narrow) {
        Vector<u16> small;
        if (!small.TryResize(count)) return "mesh: out of memory";
        for (u32 i = 0; i < count; ++i) small[i] = static_cast<u16>(indices[i]);
        return out->Set(source.Layout(), source.PositionOffset(), source.Vertices(), source.VertexCount(), rhi::IndexType::UInt16, small.Data(), count, topology);
    }
    return out->Set(source.Layout(), source.PositionOffset(), source.Vertices(), source.VertexCount(), rhi::IndexType::UInt32, indices, count, topology);
}

struct DirectedEdge {
    u64 key; // (from << 32) | to
    u32 triangle;
    u32 third; // the triangle's third vertex
};

int CompareEdges(const void* a, const void* b) {
    const u64 ka = static_cast<const DirectedEdge*>(a)->key;
    const u64 kb = static_cast<const DirectedEdge*>(b)->key;
    return ka < kb ? -1 : (ka > kb ? 1 : 0);
}

} // namespace

const char* StripToList(const MeshData& strip, MeshData* out) {
    ENGINE_ASSERT(out != nullptr && out != &strip, "StripToList: out is null or the source");
    if (!strip.Valid() || strip.Topology() != rhi::PrimitiveTopology::TriangleStrip) return "strip to list: the mesh is not a triangle strip";
    const u32 restart = strip.IndexFormat() == rhi::IndexType::UInt16 ? 0xFFFFu : 0xFFFFFFFFu;
    Vector<u32> list;
    if (!list.TryResize(static_cast<usize>(strip.IndexCount()) * 3)) return "strip to list: out of memory";
    u32 n = 0;
    u32 position = 0; // position inside the current strip
    u32 window[3] = {0, 0, 0};
    for (u32 i = 0; i < strip.IndexCount(); ++i) {
        const u32 index = strip.IndexAt(i);
        if (index == restart) {
            position = 0;
            continue;
        }
        window[position % 3] = index;
        ++position;
        if (position < 3) continue;
        const u32 k = position - 3; // triangle number inside the strip
        const u32 a = window[k % 3];
        const u32 b = window[(k + 1) % 3];
        const u32 c = window[(k + 2) % 3];
        if (a == b || b == c || a == c) continue; // a degenerate join
        if (k % 2 == 0) {
            list[n++] = a;
            list[n++] = b;
        } else {
            list[n++] = b;
            list[n++] = a;
        }
        list[n++] = c;
    }
    return n == 0 ? "strip to list: the strip holds no triangle" : SetIndices(strip, list.Data(), n, rhi::PrimitiveTopology::TriangleList, out);
}

const char* ListToStrip(const MeshData& list, StripJoin join, MeshData* out) {
    ENGINE_ASSERT(out != nullptr && out != &list, "ListToStrip: out is null or the source");
    if (!list.Valid() || list.Topology() != rhi::PrimitiveTopology::TriangleList) return "list to strip: the mesh is not a triangle list";
    const u32 triangles = list.IndexCount() / 3;
    Vector<DirectedEdge> edges_storage;
    Vector<bool> used_storage;
    // A lone triangle costs at most 3 indices plus a 3-index join.
    Vector<u32> result_storage;
    Vector<u32> strip_storage;
    if (!edges_storage.TryResize(static_cast<usize>(triangles) * 3) || !used_storage.TryResize(triangles) ||
        !result_storage.TryResize(static_cast<usize>(triangles) * 6 + 4) || !strip_storage.TryResize(static_cast<usize>(triangles) + 2)) {
        return "list to strip: out of memory";
    }
    DirectedEdge* edges = edges_storage.Data();
    bool* used = used_storage.Data();
    u32* result = result_storage.Data();
    u32* strip = strip_storage.Data();
    for (u32 t = 0; t < triangles; ++t) {
        used[t] = false;
        const u32 v[3] = {list.IndexAt(t * 3), list.IndexAt(t * 3 + 1), list.IndexAt(t * 3 + 2)};
        for (u32 e = 0; e < 3; ++e) {
            edges[t * 3 + e] = {(static_cast<u64>(v[e]) << 32) | v[(e + 1) % 3], t, v[(e + 2) % 3]};
        }
    }
    std::qsort(edges, static_cast<usize>(triangles) * 3, sizeof(DirectedEdge), &CompareEdges);

    auto find = [&](u32 from, u32 to) -> const DirectedEdge* {
        const u64 key = (static_cast<u64>(from) << 32) | to;
        usize lo = 0;
        usize hi = static_cast<usize>(triangles) * 3;
        while (lo < hi) {
            const usize mid = (lo + hi) / 2;
            if (edges[mid].key < key) lo = mid + 1;
            else hi = mid;
        }
        // Equal keys (a non-manifold mesh) are tried in order until an unused triangle is found.
        for (; lo < static_cast<usize>(triangles) * 3 && edges[lo].key == key; ++lo) {
            if (!used[edges[lo].triangle]) return &edges[lo];
        }
        return nullptr;
    };

    u32 n = 0;
    // A list that used all 65536 16-bit values comes out in 32-bit indices (see below), so the marker has the output's width.
    const bool widen = list.IndexFormat() == rhi::IndexType::UInt16 && list.VertexCount() > 65535;
    const u32 restart = (list.IndexFormat() == rhi::IndexType::UInt16 && !widen) ? 0xFFFFu : 0xFFFFFFFFu;
    for (u32 t = 0; t < triangles; ++t) {
        if (used[t]) continue;
        used[t] = true;
        u32 length = 0;
        strip[length++] = list.IndexAt(t * 3);
        strip[length++] = list.IndexAt(t * 3 + 1);
        strip[length++] = list.IndexAt(t * 3 + 2);
        for (;;) {
            // The next triangle (number length - 2) is drawn as (p, q, r) when that number is even and (q, p, r) when odd.
            const u32 p = strip[length - 2];
            const u32 q = strip[length - 1];
            const u32 number = length - 2;
            const DirectedEdge* next = (number % 2 == 0) ? find(p, q) : find(q, p);
            if (next == nullptr) break;
            used[next->triangle] = true;
            strip[length++] = next->third;
        }
        if (n != 0) {
            if (join == StripJoin::Restart) {
                result[n++] = restart;
            } else {
                const u32 last = result[n - 1];
                result[n++] = last;
                result[n++] = strip[0];
                if (n % 2 != 0) result[n++] = strip[0]; // the next real triangle must start at an even position
            }
        }
        for (u32 i = 0; i < length; ++i) result[n++] = strip[i];
    }
    const char* error = nullptr;
    if (widen) {
        // The list used all 65536 16-bit values; a strip cannot (0xFFFF restarts), so the result uses 32-bit indices.
        error = out->Set(list.Layout(), list.PositionOffset(), list.Vertices(), list.VertexCount(), rhi::IndexType::UInt32, result, n, rhi::PrimitiveTopology::TriangleStrip);
    } else {
        error = SetIndices(list, result, n, rhi::PrimitiveTopology::TriangleStrip, out);
    }
    return error;
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
        topology_ = other.topology_;
        has_restart_ = other.has_restart_;
        layout_ = other.layout_;
        bounds_ = other.bounds_;
        sort_id_ = other.sort_id_;
        other.sort_id_ = 0;
        ready_ = other.ready_;
        prepared_ = other.prepared_;
        other.device_ = nullptr;
        other.vertex_buffer_ = {};
        other.index_buffer_ = {};
        other.vertex_count_ = 0;
        other.index_count_ = 0;
        other.ready_ = false;
        other.prepared_ = false;
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
    topology_ = data.Topology();
    has_restart_ = data.HasPrimitiveRestart();
    layout_ = data.Layout();
    bounds_ = data.GetBounds();
    static std::atomic<u32> next_sort_id{1};
    sort_id_ = next_sort_id.fetch_add(1, std::memory_order_relaxed) % 0xffffffu + 1; // 1 to 2^24 - 1, never 0
    ready_ = false;
    prepared_ = false;
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
    sort_id_ = 0;
    ready_ = false;
    prepared_ = false;
    device_ = nullptr;
}

void GpuMesh::RecordPrepare(rhi::CommandList* list) {
    ENGINE_ASSERT(Valid() && list != nullptr, "GpuMesh::RecordPrepare: empty mesh or null list");
    if (ready_) {
        return;
    }
    list->Barrier(vertex_buffer_, rhi::ResourceState::CopyDst, rhi::ResourceState::VertexBuffer);
    list->Barrier(index_buffer_, rhi::ResourceState::CopyDst, rhi::ResourceState::IndexBuffer);
    prepared_ = true;
}

void GpuMesh::Draw(rhi::CommandList* list, u32 instance_count, u32 first_instance) const {
    ENGINE_ASSERT(Valid() && (prepared_ || ready_), "GpuMesh::Draw: the mesh is empty or RecordPrepare was never called");
    list->SetVertexBuffer(0, vertex_buffer_, 0);
    list->SetIndexBuffer(index_buffer_, 0, index_type_);
    list->DrawIndexed(index_count_, instance_count, 0, 0, first_instance);
}

} // namespace engine_placeholder::renderer
