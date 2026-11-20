#include "asset/mesh_collider.h"

#include "cstr/mem_ops.h"
#include "lang/scalar.h"

namespace vse::asset
{
namespace
{

void McolStore32(byte* p, u32 value)
{
    for (u32 i = 0; i < 4; ++i)
    {
        p[i] = static_cast<byte>(value >> (8 * i));
    }
}

u32 McolLoad32(const byte* p)
{
    u32 v = 0;
    for (u32 i = 0; i < 4; ++i)
    {
        v |= static_cast<u32>(p[i]) << (8 * i);
    }
    return v;
}

Status McolBad(const char* message)
{
    return Status::Make(ErrorCode::Invalid, message);
}

Status McolCorrupt(const char* message)
{
    return Status::Make(ErrorCode::Corrupt, message);
}

bool McolFinite3(const f32* v)
{
    return IsFinite(v[0]) && IsFinite(v[1]) && IsFinite(v[2]);
}

}  // namespace

Status BuildMeshColliderBlob(const TypeRegistry& types, ResourceId id, Span<const Array<f32, 3>> positions,
                             Span<const u32> indices, Span<const u8> materials, Vector<byte>* out, u32 target)
{
    const usize nv = positions.Size();
    if (indices.Size() == 0 || indices.Size() % 3 != 0 || nv == 0)
    {
        return McolBad("a mesh collider needs vertices and a whole number of triangles");
    }
    const usize nt = indices.Size() / 3;
    if (nv > kMaxMeshColliderVertices || nt > kMaxMeshColliderTriangles)
    {
        return McolBad("a mesh collider is over the vertex or triangle limit");
    }
    if (materials.Size() != 0 && materials.Size() != nt)
    {
        return McolBad("the material list is not one per triangle");
    }
    Array<f32, 3> lo = {};
    Array<f32, 3> hi = {};
    for (usize i = 0; i < nv; ++i)
    {
        if (!McolFinite3(positions[i].Data()))
        {
            return McolBad("a mesh collider position is not finite");
        }
        for (u32 k = 0; k < 3; ++k)
        {
            lo[k] = (i == 0 || positions[i][k] < lo[k]) ? positions[i][k] : lo[k];
            hi[k] = (i == 0 || positions[i][k] > hi[k]) ? positions[i][k] : hi[k];
        }
    }
    for (usize t = 0; t < nt; ++t)
    {
        for (u32 c = 0; c < 3; ++c)
        {
            if (indices[t * 3 + c] >= nv)
            {
                return McolBad("a mesh collider index is outside the vertices");
            }
        }
        if (!MeshColliderTriangleHasArea(positions[indices[t * 3]], positions[indices[t * 3 + 1]],
                                         positions[indices[t * 3 + 2]]))
        {
            return McolBad("a mesh collider triangle has no area");
        }
    }
    Vector<byte> header;
    header.Resize(kMeshColliderHeaderBytes);
    MemSet(header.Data(), 0, header.Size());
    McolStore32(header.Data(), static_cast<u32>(nv));
    McolStore32(header.Data() + 4, static_cast<u32>(nt));
    MemCopy(header.Data() + 8, lo.Data(), 12);
    MemCopy(header.Data() + 20, hi.Data(), 12);
    Vector<byte> pos;
    pos.Resize(nv * 12);
    for (usize i = 0; i < nv; ++i)
    {
        MemCopy(pos.Data() + i * 12, positions[i].Data(), 12);
    }
    Vector<byte> idx;
    idx.Resize(nt * 12);
    for (usize k = 0; k < nt * 3; ++k)
    {
        McolStore32(idx.Data() + k * 4, indices[k]);
    }
    Vector<byte> mat;
    mat.Resize(nt);
    for (usize t = 0; t < nt; ++t)
    {
        mat[t] = materials.Size() != 0 ? static_cast<byte>(materials[t]) : byte{0};
    }
    BlobWriter writer(types, types::kMeshCollider, id, target);
    ENGINE_TRY(writer.AddSection(kMeshColliderSectionHeader, ByteSpan(header.Data(), header.Size())));
    ENGINE_TRY(writer.AddSection(kMeshColliderSectionPositions, ByteSpan(pos.Data(), pos.Size())));
    ENGINE_TRY(writer.AddSection(kMeshColliderSectionIndices, ByteSpan(idx.Data(), idx.Size())));
    ENGINE_TRY(writer.AddSection(kMeshColliderSectionMaterials, ByteSpan(mat.Data(), mat.Size())));
    return writer.Finish(out);
}

bool MeshColliderTriangleHasArea(const Array<f32, 3>& a, const Array<f32, 3>& b, const Array<f32, 3>& c)
{
    // physics::TriangleMesh::Create: n = (b - a).Cross(c - a), n.LengthSquared() (x*x + y*y + z*z), finite and > 1e-20.
    const f32 e1x = b[0] - a[0];
    const f32 e1y = b[1] - a[1];
    const f32 e1z = b[2] - a[2];
    const f32 e2x = c[0] - a[0];
    const f32 e2y = c[1] - a[1];
    const f32 e2z = c[2] - a[2];
    const f32 nx = e1y * e2z - e1z * e2y;
    const f32 ny = e1z * e2x - e1x * e2z;
    const f32 nz = e1x * e2y - e1y * e2x;
    const f32 len2 = nx * nx + ny * ny + nz * nz;
    return IsFinite(len2) && len2 > 1e-20f;
}

Status MeshColliderView::Open(const SharedBytesPtr& bytes, const TypeRegistry& types)
{
    *this = MeshColliderView();
    if (!bytes)
    {
        return McolBad("no bytes were given");
    }
    const ByteSpan data = bytes->Bytes();
    BlobReader blob;
    ENGINE_TRY(blob.Open(data, types));
    if (blob.Type() != types::kMeshCollider)
    {
        return Status::Make(ErrorCode::TypeMismatch, "the blob is not a mesh collider");
    }
    ByteSpan header;
    if (!blob.FindSection(kMeshColliderSectionHeader, &header) || header.Size() != kMeshColliderHeaderBytes)
    {
        return McolCorrupt("the mesh collider header is missing or has the wrong size");
    }
    const u32 nv = McolLoad32(header.Data());
    const u32 nt = McolLoad32(header.Data() + 4);
    if (nv == 0 || nv > kMaxMeshColliderVertices || nt == 0 || nt > kMaxMeshColliderTriangles)
    {
        return McolCorrupt("a mesh collider count is outside its limits");
    }
    ByteSpan pos;
    ByteSpan idx;
    ByteSpan mat;
    if (!blob.FindSection(kMeshColliderSectionPositions, &pos) || pos.Size() != usize{nv} * 12 ||
        !blob.FindSection(kMeshColliderSectionIndices, &idx) || idx.Size() != usize{nt} * 12 ||
        !blob.FindSection(kMeshColliderSectionMaterials, &mat) || mat.Size() != nt)
    {
        return McolCorrupt("a mesh collider section is missing or has the wrong size");
    }
    for (u32 i = 0; i < nv; ++i)
    {
        f32 p[3];
        MemCopy(p, pos.Data() + i * 12, 12);
        if (!McolFinite3(p))
        {
            return McolCorrupt("a mesh collider position is not finite");
        }
    }
    for (u32 k = 0; k < nt * 3; ++k)
    {
        if (McolLoad32(idx.Data() + k * 4) >= nv)
        {
            return McolCorrupt("a mesh collider index is outside the vertices");
        }
    }
    for (u32 t = 0; t < nt; ++t)
    {
        Array<f32, 3> corner[3];
        for (u32 c = 0; c < 3; ++c)
        {
            MemCopy(corner[c].Data(), pos.Data() + usize{McolLoad32(idx.Data() + (usize{t} * 3 + c) * 4)} * 12, 12);
        }
        if (!MeshColliderTriangleHasArea(corner[0], corner[1], corner[2]))
        {
            return McolCorrupt("a mesh collider triangle has no area");
        }
    }
    bytes_ = bytes;
    vertex_count_ = nv;
    triangle_count_ = nt;
    id_ = blob.Id();
    positions_ = static_cast<usize>(pos.Data() - data.Data());
    indices_ = static_cast<usize>(idx.Data() - data.Data());
    materials_ = static_cast<usize>(mat.Data() - data.Data());
    return Status::Success();
}

Array<f32, 3> MeshColliderView::Position(u32 i) const
{
    ENGINE_ASSERT(i < vertex_count_, "MeshColliderView::Position: index out of range");
    Array<f32, 3> v;
    MemCopy(v.Data(), Base() + positions_ + usize{i} * 12, 12);
    return v;
}

u32 MeshColliderView::Index(u32 t, u32 corner) const
{
    ENGINE_ASSERT(t < triangle_count_ && corner < 3, "MeshColliderView::Index: out of range");
    return McolLoad32(Base() + indices_ + (usize{t} * 3 + corner) * 4);
}

u8 MeshColliderView::Material(u32 t) const
{
    ENGINE_ASSERT(t < triangle_count_, "MeshColliderView::Material: out of range");
    return static_cast<u8>(Base()[materials_ + t]);
}

}  // namespace vse::asset
