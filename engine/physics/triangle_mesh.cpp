#include "physics/triangle_mesh.h"

#include "algorithm.h"
#include "lang/scalar.h"

namespace vse::physics
{
namespace
{

struct EdgeRecord
{
    u64 key = 0;  // the two vertex indices, smallest first
    u32 triangle = 0;
    u32 edge = 0;
};

bool Finite(const Vec3& v)
{
    return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
}

}  // namespace

TriangleMeshConstPtr TriangleMesh::Create(const TriangleMeshSpec& spec, Validation* result)
{
    Validation ignored;
    Validation& r = result != nullptr ? *result : ignored;
    r = Validation::Ok;
    const usize nv = spec.positions.Size();
    const usize ni = spec.indices.Size();
    if (nv == 0 || nv > kMaxMeshVertices || ni == 0 || ni % 3 != 0 || ni / 3 > kMaxMeshTriangles ||
        (spec.materials.Size() != 0 && spec.materials.Size() != ni / 3))
    {
        r = Validation::OutOfRange;
        return TriangleMeshConstPtr();
    }
    for (usize i = 0; i < nv; ++i)
    {
        if (!Finite(spec.positions[i]))
        {
            r = Validation::NotFinite;
            return TriangleMeshConstPtr();
        }
    }
    for (usize k = 0; k < ni; ++k)
    {
        if (spec.indices[k] >= nv)
        {
            r = Validation::OutOfRange;
            return TriangleMeshConstPtr();
        }
    }
    TriangleMeshPtr mesh = MakeShared<TriangleMesh>();
    const u32 nt = static_cast<u32>(ni / 3);
    for (usize i = 0; i < nv; ++i)
    {
        mesh->vertices_.Push(spec.positions[i]);
    }
    for (usize k = 0; k < ni; ++k)
    {
        mesh->indices_.Push(spec.indices[k]);
    }
    Vector<spatial::Item> items;
    Vector<EdgeRecord> edges;
    for (u32 t = 0; t < nt; ++t)
    {
        const Vec3& a = mesh->vertices_[mesh->indices_[t * 3]];
        const Vec3& b = mesh->vertices_[mesh->indices_[t * 3 + 1]];
        const Vec3& c = mesh->vertices_[mesh->indices_[t * 3 + 2]];
        const Vec3 n = (b - a).Cross(c - a);
        const f32 len2 = n.LengthSquared();
        if (!IsFinite(len2) || !(len2 > 1e-20f))
        {
            r = Validation::NotPositive;
            return TriangleMeshConstPtr();
        }
        mesh->normals_.Push(n * (1.0f / Sqrt(len2)));
        mesh->materials_.Push(spec.materials.Size() != 0 ? spec.materials[t] : u8{0});
        spatial::Item item;
        item.box = Aabb::FromMinMax(
            Vec3(FMin(a.x, FMin(b.x, c.x)), FMin(a.y, FMin(b.y, c.y)), FMin(a.z, FMin(b.z, c.z))),
            Vec3(FMax(a.x, FMax(b.x, c.x)), FMax(a.y, FMax(b.y, c.y)), FMax(a.z, FMax(b.z, c.z))));
        item.key = t;
        items.Push(item);
        for (u32 e = 0; e < 3; ++e)
        {
            const u32 v0 = mesh->indices_[t * 3 + e];
            const u32 v1 = mesh->indices_[t * 3 + (e + 1) % 3];
            EdgeRecord rec;
            rec.key = (u64{v0 < v1 ? v0 : v1} << 32) | u64{v0 < v1 ? v1 : v0};
            rec.triangle = t;
            rec.edge = e;
            edges.Push(rec);
        }
    }
    Sort(Span<EdgeRecord>(edges.Data(), edges.Size()),
         [](const EdgeRecord& x, const EdgeRecord& y)
         {
             return x.key != y.key ? x.key < y.key
                                   : (x.triangle != y.triangle ? x.triangle < y.triangle : x.edge < y.edge);
         });
    mesh->neighbors_.Resize(usize{nt} * 3);
    for (usize i = 0; i < mesh->neighbors_.Size(); ++i)
    {
        mesh->neighbors_[i] = kNoTriangle;
    }
    usize i = 0;
    while (i < edges.Size())
    {
        usize j = i;
        while (j < edges.Size() && edges[j].key == edges[i].key)
        {
            ++j;
        }
        if (j - i == 2 && edges[i].triangle != edges[i + 1].triangle)
        {
            mesh->neighbors_[edges[i].triangle * 3 + edges[i].edge] = edges[i + 1].triangle;
            mesh->neighbors_[edges[i + 1].triangle * 3 + edges[i + 1].edge] = edges[i].triangle;
        }
        i = j;
    }
    mesh->bvh_.Build(Span<const spatial::Item>(items.Data(), items.Size()));
    mesh->bounds_ = mesh->bvh_.Bounds().Narrow();  // the boxes are unions of single-precision ones: exact
    mesh->extent_ = Vec3(1e-3f, 1e-3f, 1e-3f);     // a flat mesh still has a box a shape can validate
    for (usize v = 0; v < nv; ++v)
    {
        mesh->extent_ = Vec3(FMax(mesh->extent_.x, Abs(mesh->vertices_[v].x)),
                             FMax(mesh->extent_.y, Abs(mesh->vertices_[v].y)),
                             FMax(mesh->extent_.z, Abs(mesh->vertices_[v].z)));
    }
    return mesh;
}

const Vec3& TriangleMesh::Vertex(u32 i) const
{
    ENGINE_ASSERT(i < vertices_.Size(), "TriangleMesh::Vertex: index out of range");
    return vertices_[i];
}

u32 TriangleMesh::Index(u32 t, u32 corner) const
{
    ENGINE_ASSERT(t < TriangleCount() && corner < 3, "TriangleMesh::Index: out of range");
    return indices_[usize{t} * 3 + corner];
}

const Vec3& TriangleMesh::Normal(u32 t) const
{
    ENGINE_ASSERT(t < TriangleCount(), "TriangleMesh::Normal: triangle out of range");
    return normals_[t];
}

u8 TriangleMesh::Material(u32 t) const
{
    ENGINE_ASSERT(t < TriangleCount(), "TriangleMesh::Material: triangle out of range");
    return materials_[t];
}

u32 TriangleMesh::Neighbor(u32 t, u32 edge) const
{
    ENGINE_ASSERT(t < TriangleCount() && edge < 3, "TriangleMesh::Neighbor: out of range");
    return neighbors_[usize{t} * 3 + edge];
}

u32 TriangleMesh::QueryBox(const Aabb& box, Vector<u32>* out) const
{
    const usize first = out->Size();
    const Aabbd wide = box;
    bvh_.Traverse([&](const Aabbd& node)
                  { return Overlaps(node, wide) ? spatial::NodeVisit::Descend : spatial::NodeVisit::Reject; },
                  [&](const spatial::Item& item)
                  {
                      if (Overlaps(item.box, wide))
                      {
                          out->Push(static_cast<u32>(item.key));
                      }
                      return true;
                  });
    Sort(Span<u32>(out->Data() + first, out->Size() - first));
    return static_cast<u32>(out->Size() - first);
}

u32 MeshLibrary::Add(const TriangleMeshConstPtr& mesh)
{
    if (!mesh)
    {
        return 0;
    }
    meshes_.Push(mesh);
    return static_cast<u32>(meshes_.Size());
}

const TriangleMesh* MeshLibrary::Find(u32 id) const
{
    return (id >= 1 && id <= meshes_.Size()) ? meshes_[id - 1].Get() : nullptr;
}

TriangleMeshConstPtr MeshLibrary::Share(u32 id) const
{
    return (id >= 1 && id <= meshes_.Size()) ? meshes_[id - 1] : TriangleMeshConstPtr();
}

}  // namespace vse::physics
