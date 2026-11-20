// The mesh-collider format and its minter (Phase 25 Day 394).

#include <catch2/catch_test_macros.hpp>

#include "asset/cpu_loaders.h"
#include "asset/memory_backend.h"
#include "asset/mesh_collider.h"
#include "asset/resource_manager.h"
#include "physics/triangle_mesh.h"
#include "cstr/mem_ops.h"
#include "cstr/cstr.h"
#include "lang/limits.h"
#include "mcol_source.h"
#include "support.h"

namespace engine_placeholder::asset
{

namespace
{

Array<f32, 3> McolP(f32 x, f32 y, f32 z)
{
    Array<f32, 3> p = {{x, y, z}};
    return p;
}

Vector<byte> McolQuad(const TypeRegistry& types)
{
    Vector<Array<f32, 3>> pos;
    pos.Push(McolP(0, 0, 0));
    pos.Push(McolP(1, 0, 0));
    pos.Push(McolP(1, 0, 1));
    pos.Push(McolP(0, 0, 1));
    const u32 idx[6] = {0, 2, 1, 0, 3, 2};
    const u8 mat[2] = {3, 4};
    Vector<byte> blob;
    REQUIRE(BuildMeshColliderBlob(types, ResourceId::FromValue(9), Span<const Array<f32, 3>>(pos.Data(), pos.Size()),
                                  Span<const u32>(idx, 6), Span<const u8>(mat, 2), &blob)
                .Ok());
    return blob;
}

Status McolOpen(const TypeRegistry& types, const Vector<byte>& blob)
{
    MeshColliderView view;
    return view.Open(CopyShared(ByteSpan(blob.Data(), blob.Size())), types);
}

}  // namespace

TEST_CASE("MCOL: a quad mints, reads back and mints identically", "[asset][mcol]")
{
    const TypeRegistry types;
    const Vector<byte> blob = McolQuad(types);
    MeshColliderView view;
    REQUIRE(view.Open(CopyShared(ByteSpan(blob.Data(), blob.Size())), types).Ok());
    CHECK(view.VertexCount() == 4);
    CHECK(view.TriangleCount() == 2);
    CHECK(view.Position(2)[2] == 1.0f);
    CHECK(view.Index(1, 1) == 3);
    CHECK(view.Material(0) == 3);
    CHECK(view.Material(1) == 4);
    const Vector<byte> again = McolQuad(types);
    REQUIRE(again.Size() == blob.Size());
    bool same = true;
    for (usize i = 0; i < blob.Size(); ++i)
    {
        same = same && blob[i] == again[i];
    }
    CHECK(same);
}

TEST_CASE("MCOL: hostile input is refused", "[asset][mcol]")
{
    const TypeRegistry types;
    Vector<Array<f32, 3>> pos;
    pos.Push(McolP(0, 0, 0));
    pos.Push(McolP(1, 0, 0));
    pos.Push(McolP(0, 0, 1));
    const auto build = [&](const Vector<Array<f32, 3>>& p, const u32* idx, usize n)
    {
        Vector<byte> blob;
        return BuildMeshColliderBlob(types, ResourceId::FromValue(1), Span<const Array<f32, 3>>(p.Data(), p.Size()),
                                     Span<const u32>(idx, n), Span<const u8>(), &blob)
            .Ok();
    };
    const u32 good[3] = {0, 1, 2};
    CHECK(build(pos, good, 3));
    const u32 bad_index[3] = {0, 1, 3};
    CHECK(!build(pos, bad_index, 3));
    const u32 partial[2] = {0, 1};
    CHECK(!build(pos, partial, 2));
    const u32 degenerate[3] = {0, 1, 1};
    CHECK(!build(pos, degenerate, 3));
    Vector<Array<f32, 3>> nan;
    nan.Push(McolP(0, 0, 0));
    nan.Push(McolP(Limits<f32>::QuietNaN(), 0, 0));
    nan.Push(McolP(0, 0, 1));
    CHECK(!build(nan, good, 3));
    CHECK(!build(Vector<Array<f32, 3>>(), good, 3));

    // A reader refuses a blob whose index was damaged after minting.
    Vector<byte> blob = McolQuad(types);
    CHECK(McolOpen(types, blob).Ok());
    BlobReader reader;
    REQUIRE(reader.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    ByteSpan idx;
    REQUIRE(reader.FindSection(kMeshColliderSectionIndices, &idx));
    Vector<byte> broken_idx;
    for (usize i = 0; i < idx.Size(); ++i)
    {
        broken_idx.Push(idx.Data()[i]);
    }
    broken_idx[0] = byte{200};
    ByteSpan header;
    ByteSpan positions;
    ByteSpan materials;
    REQUIRE(reader.FindSection(kMeshColliderSectionHeader, &header));
    REQUIRE(reader.FindSection(kMeshColliderSectionPositions, &positions));
    REQUIRE(reader.FindSection(kMeshColliderSectionMaterials, &materials));
    BlobWriter writer(types, types::kMeshCollider, reader.Id(), 0);
    REQUIRE(writer.AddSection(kMeshColliderSectionHeader, header).Ok());
    REQUIRE(writer.AddSection(kMeshColliderSectionPositions, positions).Ok());
    REQUIRE(writer.AddSection(kMeshColliderSectionIndices, ByteSpan(broken_idx.Data(), broken_idx.Size())).Ok());
    REQUIRE(writer.AddSection(kMeshColliderSectionMaterials, materials).Ok());
    Vector<byte> damaged;
    REQUIRE(writer.Finish(&damaged).Ok());
    CHECK(McolOpen(types, damaged).Code() == ErrorCode::Corrupt);
    // A truncated blob and another type.
    Vector<byte> cut;
    for (usize i = 0; i + 8 < blob.Size(); ++i)
    {
        cut.Push(blob[i]);
    }
    CHECK(McolOpen(types, cut).Failed());
    MeshColliderView none;
    CHECK(none.Open(SharedBytesPtr(), types).Failed());
}

TEST_CASE("MCOL: the text source mints and reports a bad line", "[asset][mcol]")
{
    const TypeRegistry types;
    const char* text = "id 000000000000000a\n# a ramp\nv 0 0 0\nv 1 0 0\nv 1 1 1\nt 0 1 2 5\n";
    Vector<byte> blob;
    McolSourceError error;
    REQUIRE(MintMeshCollider(StringView(text, CStrLength(text)), types, &blob, &error).Ok());
    MeshColliderView view;
    REQUIRE(view.Open(CopyShared(ByteSpan(blob.Data(), blob.Size())), types).Ok());
    CHECK(view.TriangleCount() == 1);
    CHECK(view.Material(0) == 5);
    const char* bad = "id 000000000000000a\nv 0 0 0\nt 0 1\n";
    CHECK(MintMeshCollider(StringView(bad, CStrLength(bad)), types, &blob, &error).Failed());
    CHECK(error.line == 3);
    const char* noid = "v 0 0 0\n";
    CHECK(MintMeshCollider(StringView(noid, CStrLength(noid)), types, &blob, &error).Failed());
    const char* material = "id 000000000000000a\nv 0 0 0\nv 1 0 0\nv 0 0 1\nt 0 1 2 300\n";
    CHECK(MintMeshCollider(StringView(material, CStrLength(material)), types, &blob, &error).Failed());
}

// glTF colliders Day 2: one rule for "a triangle with no area", the physics module's.
TEST_CASE("MCOL: the area rule is physics' own", "[asset][mcol]")
{
    // A triangle of area about 1e-11 (cross squared about 4e-22): the old f64 1e-24 test accepted it, physics does not.
    CHECK(!MeshColliderTriangleHasArea(McolP(0, 0, 0), McolP(1e-5f, 0, 0), McolP(0, 2e-6f, 0)));
    CHECK(MeshColliderTriangleHasArea(McolP(0, 0, 0), McolP(1, 0, 0), McolP(0, 1, 0)));
    CHECK(!MeshColliderTriangleHasArea(McolP(0, 0, 0), McolP(1, 1, 1), McolP(2, 2, 2)));  // collinear
    CHECK(!MeshColliderTriangleHasArea(McolP(0, 0, 0), McolP(Limits<f32>::Infinity(), 0, 0), McolP(0, 1, 0)));
    // 1,000 random triangles near the threshold: the same answer as a one-triangle physics mesh.
    u64 state = 20261120;
    auto below = [&state](u32 bound)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<u32>((state >> 33) % bound);
    };
    for (u32 n = 0; n < 1000; ++n)
    {
        const f32 scale = static_cast<f32>(below(1000) + 1) * 1e-8f;
        Array<f32, 3> c[3];
        Vec3 v[3];
        for (u32 k = 0; k < 3; ++k)
        {
            for (u32 a = 0; a < 3; ++a)
            {
                c[k][a] = (static_cast<f32>(below(2001)) - 1000.0f) * scale;
            }
            v[k] = Vec3(c[k][0], c[k][1], c[k][2]);
        }
        const u32 idx[3] = {0, 1, 2};
        physics::TriangleMeshSpec spec;
        spec.positions = Span<const Vec3>(v, 3);
        spec.indices = Span<const u32>(idx, 3);
        const bool physics_accepts = static_cast<bool>(physics::TriangleMesh::Create(spec, nullptr));
        CHECK(MeshColliderTriangleHasArea(c[0], c[1], c[2]) == physics_accepts);
    }
}

TEST_CASE("MCOL: a blob with a triangle physics would refuse does not open", "[asset][mcol]")
{
    const TypeRegistry types;
    Vector<byte> blob = McolQuad(types);
    // Move vertex 1 onto vertex 0 in the stored positions, after the builder checked them: Open refuses it.
    BlobReader reader;
    REQUIRE(reader.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    ByteSpan positions;
    REQUIRE(reader.FindSection(kMeshColliderSectionPositions, &positions));
    const usize at = static_cast<usize>(positions.Data() - blob.Data());
    MemCopy(blob.Data() + at + 12, blob.Data() + at, 12);
    MeshColliderView view;
    const Status opened = view.Open(CopyShared(ByteSpan(blob.Data(), blob.Size())), types);
    CHECK(opened.Failed());
    CHECK(StringView(opened.Message()).Contains(StringView("no area")));  // the area check, not another one
}

TEST_CASE("MCOL: it loads through the resource manager", "[asset][mcol]")
{
    const TypeRegistry types;
    const Vector<byte> blob = McolQuad(types);
    HostMemoryBackend backend(types, u64{1} << 20);
    const StringView paths[1] = {StringView("/colliders/quad")};
    MemoryAddOptions add;
    add.paths = Span<const StringView>(paths, 1);
    REQUIRE(backend.Add(ResourceId::FromValue(9), ByteSpan(blob.Data(), blob.Size()), add).Ok());
    ManagerConfig config;
    config.buffer_pool_bytes = u64{1} << 20;
    ResourceManager manager(types, config);
    RegisterCpuLoaders(&manager);
    REQUIRE(manager.Mount(StringView("/"), &backend, 0).Ok());
    REQUIRE(manager.Mounts().RefreshTables().Ok());
    Handle<MeshColliderView> by_id;
    REQUIRE(manager.Load<MeshColliderView>(ResourceId::FromValue(9), &by_id).Ok());
    CHECK(by_id.Get()->TriangleCount() == 2);
    Handle<MeshColliderView> by_path;
    REQUIRE(manager.LoadPath<MeshColliderView>(StringView("/colliders/quad"), &by_path).Ok());
    CHECK(by_path.Get()->VertexCount() == 4);
}

}  // namespace engine_placeholder::asset
