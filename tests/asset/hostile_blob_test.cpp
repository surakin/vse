// Phase 22 Day 340: every blob reader against mutated input: counts bounded, nothing read outside the bytes, no crash
// under the sanitizers.

#include <catch2/catch_test_macros.hpp>

#include "asset/clip.h"
#include "asset/material.h"
#include "asset/mesh.h"
#include "asset/skeleton.h"
#include "asset/texture.h"
#include "asset_import/gltf_import.h"
#include "cstr/format.h"
#include "gltf_writer.h"
#include "png_writer.h"

namespace engine_placeholder::asset
{
namespace
{

Vector<byte> HostileGlb()
{
    test::GltfWriter w;
    const f32 positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const f32 uvs[6] = {0, 0, 1, 0, 0, 1};
    const u16 indices[3] = {0, 1, 2};
    const u8 joints[12] = {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    const f32 weights[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    const f32 times[2] = {0, 1};
    const f32 moves[6] = {0, 1, 0, 0, 2, 0};
    test::PngSpec png;
    png.width = 8;
    png.height = 8;
    for (u32 i = 0; i < 8 * 8 * 4; ++i)
    {
        png.rows.Push(static_cast<byte>(i * 5));
    }
    const Vector<byte> png_bytes = test::MakePng(png);
    const u32 pos = w.AddAccessor(w.AddView(positions, sizeof(positions)), 5126, 3, "VEC3");
    const u32 uv = w.AddAccessor(w.AddView(uvs, sizeof(uvs)), 5126, 3, "VEC2");
    const u32 idx = w.AddAccessor(w.AddView(indices, sizeof(indices)), 5123, 3, "SCALAR");
    const u32 jnt = w.AddAccessor(w.AddView(joints, sizeof(joints)), 5121, 3, "VEC4");
    const u32 wgt = w.AddAccessor(w.AddView(weights, sizeof(weights)), 5126, 3, "VEC4");
    const u32 t = w.AddAccessor(w.AddView(times, sizeof(times)), 5126, 2, "SCALAR");
    const u32 mv = w.AddAccessor(w.AddView(moves, sizeof(moves)), 5126, 2, "VEC3");
    const u32 image = w.AddView(png_bytes.Data(), png_bytes.Size());
    char json[1600];
    FormatTo(
        json, sizeof(json),
        "\"nodes\":[{\"name\":\"r\",\"children\":[1]},{\"name\":\"t\"},{\"mesh\":0,\"skin\":0}],"
        "\"skins\":[{\"joints\":[0,1]}],"
        "\"images\":[{\"bufferView\":%u}],\"textures\":[{\"source\":0}],"
        "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}},\"normalTexture\":{\"index\":0}}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%u,\"TEXCOORD_0\":%u,\"JOINTS_0\":%u,\"WEIGHTS_0\":%u},"
        "\"indices\":%u,\"material\":0}]}],"
        "\"animations\":[{\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}}],"
        "\"samplers\":[{\"input\":%u,\"output\":%u}]}]",
        image, pos, uv, jnt, wgt, idx, t, mv);
    w.AddRoot(json);
    return w.Glb();
}

Vector<byte> Mutate(const Vector<byte>& blob, u64* state, u32 flips)
{
    Vector<byte> out;
    for (usize i = 0; i < blob.Size(); ++i)
    {
        out.Push(blob[i]);
    }
    for (u32 f = 0; f < flips; ++f)
    {
        *state = *state * 6364136223846793005ull + 1442695040888963407ull;
        out[static_cast<usize>((*state >> 33) % out.Size())] = static_cast<byte>(*state >> 17);
    }
    return out;
}

}  // namespace

TEST_CASE("hostile: every blob reader survives mutated and truncated blobs", "[asset][hostile]")
{
    const Vector<byte> glb = HostileGlb();
    NoGltfSource none;
    SeededIdSource ids(4);
    ImportOptions options;
    options.ids = &ids;
    ImportResult result;
    GltfError error;
    REQUIRE(ImportGltf(ByteSpan(glb.Data(), glb.Size()), none, options, &result, &error).Ok());
    REQUIRE(result.resources.Size() == 6);  // two textures, a material, a skeleton, a mesh and a clip
    TypeRegistry types;
    u64 state = 99;
    f64 sink = 0;  // what the readers return is used, so that reading it is not optimized away
    for (usize r = 0; r < result.resources.Size(); ++r)
    {
        const ImportedResource& resource = result.resources[r];
        for (u32 n = 0; n < 1500; ++n)
        {
            const Vector<byte> bad = Mutate(resource.blob, &state, 1 + n % 5);
            const ByteSpan bytes(bad.Data(), bad.Size());
            if (resource.type == types::kMesh)
            {
                MeshView view;
                if (view.Open(bytes, types).Ok())
                {
                    const Status checked = view.CheckIndices();  // a pass over the data: no read outside it
                    CHECK((checked.Ok() || checked.Is(ErrorCode::Corrupt)));
                }
            }
            else if (resource.type == types::kSkeleton)
            {
                SkeletonView view;
                if (view.Open(bytes, types).Ok())
                {
                    for (u32 j = 0; j < view.JointCount(); ++j)
                    {
                        sink += static_cast<f64>(view.Name(j).Length()) + static_cast<f64>(view.InverseBind(j)[0]);
                    }
                }
            }
            else if (resource.type == types::kAnimation)
            {
                ClipView view;
                if (view.Open(bytes, types).Ok())
                {
                    for (u32 t = 0; t < view.TrackCount(); ++t)
                    {
                        sink += static_cast<f64>(view.Sample(t, 0.5f).translation[0]);
                    }
                }
            }
            else if (resource.type == types::kTexture)
            {
                TextureView view;
                if (view.Open(bytes, types).Ok())
                {
                    sink += static_cast<f64>(view.Level(0, 0).bytes.Size());
                }
            }
            else if (resource.type == types::kMaterial)
            {
                MaterialAssetData data;
                const Status loaded = LoadMaterialBlob(bytes, types, &data);
                CHECK((loaded.Ok() || loaded.Failed()));
            }
        }
        INFO("sink " << sink);
        for (usize cut = 0; cut < resource.blob.Size(); cut += 1 + resource.blob.Size() / 400)
        {
            MeshView mesh;
            SkeletonView skeleton;
            ClipView clip;
            TextureView texture;
            MaterialAssetData data;
            const ByteSpan bytes(resource.blob.Data(), cut);
            CHECK(mesh.Open(bytes, types).Failed());
            CHECK(skeleton.Open(bytes, types).Failed());
            CHECK(clip.Open(bytes, types).Failed());
            CHECK(texture.Open(bytes, types).Failed());
            CHECK(LoadMaterialBlob(bytes, types, &data).Failed());
        }
    }
}

}  // namespace engine_placeholder::asset
