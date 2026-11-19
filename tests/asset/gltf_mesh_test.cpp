// Phase 22 Day 336: importing glTF meshes (asset_import/gltf_mesh.h) and the first in-memory loop: a glTF in memory,
// mesh blobs in a HostMemoryBackend, the resource manager loading them.

#include <catch2/catch_test_macros.hpp>

#include "asset/async_loader.h"
#include "asset/cpu_loaders.h"
#include "asset/memory_backend.h"
#include "animation/components.h"
#include "ecs/schedule.h"
#include "asset/level.h"
#include "asset/material.h"
#include "asset/mesh.h"
#include "storage/builtin_components.h"
#include "scene/components.h"
#include "scene/scene_components.h"
#include "asset/resource_manager.h"
#include "asset_import/gltf_mesh.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "gltf_writer.h"
#include "jobs/scheduler.h"
#include "lang/limits.h"
#include "lang/scalar.h"
#include "support.h"

namespace engine_placeholder::asset
{
namespace
{

using test::GltfWriter;

u32 GmComponentsOf(const char* type)
{
    const StringView name(type);
    return name == StringView("SCALAR") ? 1 : static_cast<u32>(type[3] - '0');
}

// An accessor over `count` array elements (the accessor's own count is the number of whole vectors in them).
template <typename T>
u32 GmAcc(GltfWriter* w, const T* values, usize count, u32 component_type, const char* type, const char* extra = "")
{
    return w->AddAccessor(w->AddView(values, count * sizeof(T)), component_type,
                          static_cast<u32>(count) / GmComponentsOf(type), type, extra);
}

const f32 GmkTrianglePositions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
const f32 GmkTriangleUvs[6] = {0, 0, 1, 0, 0, 1};

// A glb with one mesh `name` of one triangle (POSITION only unless `full`).
Vector<byte> GmTriangleGlb(const char* name, bool full)
{
    GltfWriter w;
    const u16 indices[3] = {0, 1, 2};
    const f32 normals[9] = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    const u32 pos = GmAcc(&w, GmkTrianglePositions, 9, 5126, "VEC3");
    const u32 idx = GmAcc(&w, indices, 3, 5123, "SCALAR");
    char json[400];
    if (full)
    {
        const u32 nor = GmAcc(&w, normals, 9, 5126, "VEC3");
        const u32 uv = GmAcc(&w, GmkTriangleUvs, 6, 5126, "VEC2");
        FormatTo(json, sizeof(json),
                 "\"meshes\":[{\"name\":\"%s\",\"primitives\":[{\"attributes\":{\"POSITION\":%u,\"NORMAL\":%u,"
                 "\"TEXCOORD_0\":%u},\"indices\":%u}]}]",
                 name, pos, nor, uv, idx);
    }
    else
    {
        FormatTo(json, sizeof(json),
                 "\"meshes\":[{\"name\":\"%s\",\"primitives\":[{\"attributes\":{\"POSITION\":%u},"
                 "\"indices\":%u}]}]",
                 name, pos, idx);
    }
    w.AddRoot(json);
    return w.Glb();
}

Status GmImport(const Vector<byte>& glb, ImportResult* result, GltfError* error,
                ImportOptions options = ImportOptions())
{
    NoGltfSource none;
    SeededIdSource ids(7);
    if (options.ids == nullptr)
    {
        options.ids = &ids;
    }
    const Status status = ImportGltf(ByteSpan(glb.Data(), glb.Size()), none, options, result, error);
    if (status.Failed())
    {
        UNSCOPED_INFO("ImportGltf: " << GltfMessage(*error, status));
    }
    return status;
}

bool GmSays(const GltfError& error, const char* text)
{
    return StringView(error.message.Data(), CStrLength(error.message.Data())).Contains(StringView(text));
}

MeshVertex GmVertex(f32 x, f32 y, f32 z, f32 nx, f32 ny, f32 nz, f32 u, f32 v)
{
    MeshVertex vertex;
    vertex.position = {{x, y, z}};
    vertex.normal = {{nx, ny, nz}};
    vertex.tangent = {{1, 0, 0, 1}};
    vertex.uv = {{u, v}};
    return vertex;
}

Array<char, kMaxSlotNameBytes + 1> GmName(const char* text)
{
    Array<char, kMaxSlotNameBytes + 1> name = {};
    MemCopy(name.Data(), text, CStrLength(text));
    return name;
}

const ImportedResource& GmMeshOf(const ImportResult& result)
{
    for (usize i = 0; i < result.resources.Size(); ++i)
    {
        if (result.resources[i].type == types::kMesh)
        {
            return result.resources[i];
        }
    }
    return result.resources[0];
}

bool GmSameBytes(const Vector<byte>& a, const Vector<byte>& b)
{
    return a.Size() == b.Size() && MemCompare(a.Data(), b.Data(), a.Size()) == 0;
}

}  // namespace

TEST_CASE("gltf mesh: a triangle with every attribute equals the blob built from the same data", "[asset][gltf]")
{
    ImportResult result;
    GltfError error;
    REQUIRE(GmImport(GmTriangleGlb("tri", true), &result, &error).Ok());
    REQUIRE(result.resources.Size() == 1);
    CHECK(result.resources[0].type == types::kMesh);
    CHECK(result.resources[0].path.View() == StringView("/gltf/meshes/tri"));
    CHECK(result.report.warnings == 0);
    CHECK(result.manifest.Entries().Size() == 1);
    CHECK(result.manifest.Entries()[0].id == result.resources[0].id);

    const MeshVertex vertices[3] = {GmVertex(0, 0, 0, 0, 0, 1, 0, 0), GmVertex(1, 0, 0, 0, 0, 1, 1, 0),
                                    GmVertex(0, 1, 0, 0, 0, 1, 0, 1)};
    const u32 indices[3] = {0, 1, 2};
    MeshSlot slot;
    slot.name = GmName("primitive0");
    slot.index_count = 3;
    MeshBuildInput input;
    input.quality = MeshQuality::Full;  // the importer writes the layout the G-buffer reads (Phase 24 Day 383)
    input.vertices = Span<const MeshVertex>(vertices, 3);
    input.indices = Span<const u32>(indices, 3);
    input.slots = Span<const MeshSlot>(&slot, 1);
    const TypeRegistry types;
    Vector<byte> expected;
    REQUIRE(BuildMeshBlob(types, result.resources[0].id, input, &expected).Ok());
    CHECK(GmSameBytes(result.resources[0].blob, expected));
}

// glTF coverage Day 5: COLOR_0 (linear) becomes the mesh's sRGB 8-bit colour stream; a material that a coloured and an
// uncoloured mesh share gets a twin on the colour template.
TEST_CASE("gltf mesh: COLOR_0 becomes per-vertex colours and a colour-template twin of a shared material",
          "[asset][gltf]")
{
    GltfWriter w;
    const u16 indices[3] = {0, 1, 2};
    const f32 colors[12] = {1, 0, 0, 1, 0, 1, 0, 0.5f, 0, 0, 0.5f, 1};
    const u32 pos = GmAcc(&w, GmkTrianglePositions, 9, 5126, "VEC3");
    const u32 idx = GmAcc(&w, indices, 3, 5123, "SCALAR");
    const u32 col = GmAcc(&w, colors, 12, 5126, "VEC4");
    char json[700];
    FormatTo(json, sizeof(json),
             "\"materials\":[{\"name\":\"paint\"}],"
             "\"meshes\":[{\"name\":\"coloured\",\"primitives\":[{\"attributes\":{\"POSITION\":%u,\"COLOR_0\":%u},"
             "\"indices\":%u,\"material\":0}]},{\"name\":\"plain\",\"primitives\":[{\"attributes\":{\"POSITION\":%u},"
             "\"indices\":%u,\"material\":0}]}]",
             pos, col, idx, pos, idx);
    w.AddRoot(json);
    ImportResult result;
    GltfError error;
    REQUIRE(GmImport(w.Glb(), &result, &error).Ok());
    const TypeRegistry types;
    bool twin = false;
    bool plain_material = false;
    for (const ImportedResource& r : result.resources)
    {
        if (r.path.View() == StringView("/gltf/meshes/coloured"))
        {
            MeshView view;
            REQUIRE(view.Open(ByteSpan(r.blob.Data(), r.blob.Size()), types).Ok());
            CHECK(view.HasColors());
            const byte* second = view.VertexBytes().Data() + view.VertexStride();
            CHECK(second[48 + 1] == 255);  // green, sRGB of 1
            CHECK(second[48 + 3] == 128);  // alpha 0.5, linear
        }
        if (r.path.View() == StringView("/gltf/meshes/plain"))
        {
            MeshView view;
            REQUIRE(view.Open(ByteSpan(r.blob.Data(), r.blob.Size()), types).Ok());
            CHECK_FALSE(view.HasColors());
        }
        if (r.path.View() == StringView("/gltf/materials/paint_color"))
        {
            MaterialAssetData m;
            REQUIRE(LoadMaterialBlob(ByteSpan(r.blob.Data(), r.blob.Size()), types, &m).Ok());
            twin = StringView(m.template_name.text) == StringView("gbuffer_color");
        }
        plain_material = plain_material || r.path.View() == StringView("/gltf/materials/paint");
    }
    CHECK(twin);
    CHECK(plain_material);
}

TEST_CASE("gltf mesh: missing normals, tangents and texture coordinates are made, with a counted warning",
          "[asset][gltf]")
{
    ImportResult result;
    GltfError error;
    REQUIRE(GmImport(GmTriangleGlb("bare", false), &result, &error).Ok());
    CHECK(result.report.warnings == 1);
    REQUIRE(result.report.notes.Size() == 1);
    CHECK(result.report.notes[0].View().Contains(StringView("TEXCOORD_0")));
    CHECK(result.report.notes[0].View().Contains(StringView("meshes[0].primitives[0]")));
    const MeshVertex vertices[3] = {GmVertex(0, 0, 0, 0, 0, 1, 0, 0), GmVertex(1, 0, 0, 0, 0, 1, 0, 0),
                                    GmVertex(0, 1, 0, 0, 0, 1, 0, 0)};
    const u32 indices[3] = {0, 1, 2};
    MeshSlot slot;
    slot.name = GmName("primitive0");
    slot.index_count = 3;
    MeshBuildInput input;
    input.quality = MeshQuality::Full;  // the importer writes the layout the G-buffer reads (Phase 24 Day 383)
    input.vertices = Span<const MeshVertex>(vertices, 3);
    input.indices = Span<const u32>(indices, 3);
    input.slots = Span<const MeshSlot>(&slot, 1);
    const TypeRegistry types;
    Vector<byte> expected;
    REQUIRE(BuildMeshBlob(types, result.resources[0].id, input, &expected).Ok());
    CHECK(GmSameBytes(result.resources[0].blob, expected));
}

TEST_CASE("gltf mesh: tangents follow the texture coordinates and their handedness", "[asset][gltf]")
{
    // The same triangle with v flipped: the tangent is still +x and the bitangent sign turns negative.
    GltfWriter w;
    const u16 indices[3] = {0, 1, 2};
    const f32 flipped[6] = {0, 1, 1, 1, 0, 0};
    const u32 pos = GmAcc(&w, GmkTrianglePositions, 9, 5126, "VEC3");
    const u32 uv = GmAcc(&w, flipped, 6, 5126, "VEC2");
    const u32 idx = GmAcc(&w, indices, 3, 5123, "SCALAR");
    char json[300];
    FormatTo(json, sizeof(json),
             "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%u,\"TEXCOORD_0\":%u},\"indices\":%u}]}]", pos,
             uv, idx);
    w.AddRoot(json);
    ImportResult result;
    GltfError error;
    REQUIRE(GmImport(w.Glb(), &result, &error).Ok());
    MeshVertex vertices[3] = {GmVertex(0, 0, 0, 0, 0, 1, 0, 1), GmVertex(1, 0, 0, 0, 0, 1, 1, 1),
                              GmVertex(0, 1, 0, 0, 0, 1, 0, 0)};
    for (MeshVertex& v : vertices)
    {
        v.tangent = {{1, 0, 0, -1}};
    }
    const u32 expected_indices[3] = {0, 1, 2};
    MeshSlot slot;
    slot.name = GmName("primitive0");
    slot.index_count = 3;
    MeshBuildInput input;
    input.quality = MeshQuality::Full;  // the importer writes the layout the G-buffer reads (Phase 24 Day 383)
    input.vertices = Span<const MeshVertex>(vertices, 3);
    input.indices = Span<const u32>(expected_indices, 3);
    input.slots = Span<const MeshSlot>(&slot, 1);
    const TypeRegistry types;
    Vector<byte> expected;
    REQUIRE(BuildMeshBlob(types, result.resources[0].id, input, &expected).Ok());
    CHECK(GmSameBytes(result.resources[0].blob, expected));
}

TEST_CASE("gltf mesh: primitives become slots named after their materials", "[asset][gltf]")
{
    GltfWriter w;
    const f32 quad[12] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
    const u16 indices[6] = {0, 1, 2, 0, 2, 3};
    const u32 pos = GmAcc(&w, quad, 12, 5126, "VEC3");
    const u32 idx = GmAcc(&w, indices, 6, 5123, "SCALAR");
    char json[900];
    FormatTo(json, sizeof(json),
             "\"materials\":[{\"name\":\"red\"},{\"name\":\"a/very/long/material/name/past/thirty-one/bytes\"}],"
             "\"meshes\":[{\"name\":\"quads\",\"primitives\":["
             "{\"attributes\":{\"POSITION\":%u},\"indices\":%u,\"material\":0},"
             "{\"attributes\":{\"POSITION\":%u},\"indices\":%u,\"material\":1},"
             "{\"attributes\":{\"POSITION\":%u},\"indices\":%u},"
             "{\"attributes\":{\"POSITION\":%u},\"indices\":%u,\"material\":0}]}]",
             pos, idx, pos, idx, pos, idx, pos, idx);
    w.AddRoot(json);
    ImportResult result;
    GltfError error;
    REQUIRE(GmImport(w.Glb(), &result, &error).Ok());
    TypeRegistry types;
    MeshView view;
    REQUIRE(view.Open(ByteSpan(GmMeshOf(result).blob.Data(), GmMeshOf(result).blob.Size()), types).Ok());
    REQUIRE(view.SlotCount() == 4);
    CHECK(view.VertexCount() == 16);
    CHECK(view.IndexCount() == 24);
    CHECK(StringView(view.Slot(0).name.Data()) == StringView("red"));
    CHECK(StringView(view.Slot(1).name.Data()) == StringView("a_very_long_material_name_past_"));
    CHECK(StringView(view.Slot(2).name.Data()) == StringView("primitive2"));
    CHECK(StringView(view.Slot(3).name.Data()) == StringView("primitive3"));  // `red` again: the name is taken
    for (u32 s = 0; s < 4; ++s)
    {
        CHECK(view.Slot(s).first_index == s * 6);
        CHECK(view.Slot(s).index_count == 6);
    }
    CHECK(view.CheckIndices().Ok());
}

TEST_CASE("gltf mesh: strips and fans become triangle lists", "[asset][gltf]")
{
    GltfWriter w;
    const f32 points[15] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0, 2, 0, 0};
    const u8 strip[5] = {0, 1, 2, 3, 4};
    const u32 pos = GmAcc(&w, points, 15, 5126, "VEC3");
    const u32 idx = GmAcc(&w, strip, 5, 5121, "SCALAR");
    char json[400];
    FormatTo(
        json, sizeof(json),
        "\"meshes\":[{\"name\":\"s\",\"primitives\":[{\"attributes\":{\"POSITION\":%u},\"indices\":%u,\"mode\":5}]},"
        "{\"name\":\"f\",\"primitives\":[{\"attributes\":{\"POSITION\":%u},\"indices\":%u,\"mode\":6}]}]",
        pos, idx, pos, idx);
    w.AddRoot(json);
    ImportResult result;
    GltfError error;
    REQUIRE(GmImport(w.Glb(), &result, &error).Ok());
    REQUIRE(result.resources.Size() == 2);
    TypeRegistry types;
    MeshView strip_view;
    MeshView fan_view;
    REQUIRE(strip_view.Open(ByteSpan(GmMeshOf(result).blob.Data(), GmMeshOf(result).blob.Size()), types).Ok());
    REQUIRE(fan_view.Open(ByteSpan(result.resources[1].blob.Data(), result.resources[1].blob.Size()), types).Ok());
    CHECK(strip_view.IndexCount() == 9);  // 5 - 2 triangles
    CHECK(fan_view.IndexCount() == 9);
    // 16-bit indices: the strip's triangles are (0 1 2) (2 1 3) (2 3 4), the fan's (0 1 2) (0 2 3) (0 3 4).
    const u16* strip_indices = reinterpret_cast<const u16*>(strip_view.IndexBytes().Data());
    const u16* fan_indices = reinterpret_cast<const u16*>(fan_view.IndexBytes().Data());
    const u16 want_strip[9] = {0, 1, 2, 2, 1, 3, 2, 3, 4};
    const u16 want_fan[9] = {0, 1, 2, 0, 2, 3, 0, 3, 4};
    for (usize i = 0; i < 9; ++i)
    {
        CHECK(strip_indices[i] == want_strip[i]);
        CHECK(fan_indices[i] == want_fan[i]);
    }
}

TEST_CASE("gltf mesh: normals made from the faces are smooth, or flat when asked", "[asset][gltf]")
{
    // Two triangles sharing an edge, folded 90 degrees: the shared vertices' smooth normal leans between the faces.
    GltfWriter w;
    const f32 points[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
    const u16 indices[6] = {0, 1, 2, 0, 3, 1};
    const u32 pos = GmAcc(&w, points, 12, 5126, "VEC3");
    const u32 idx = GmAcc(&w, indices, 6, 5123, "SCALAR");
    char json[300];
    FormatTo(json, sizeof(json), "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%u},\"indices\":%u}]}]",
             pos, idx);
    w.AddRoot(json);
    const Vector<byte> glb = w.Glb();
    ImportResult smooth;
    ImportResult flat;
    GltfError error;
    REQUIRE(GmImport(glb, &smooth, &error).Ok());
    ImportOptions options;
    options.flat_normals = true;
    REQUIRE(GmImport(glb, &flat, &error, options).Ok());
    TypeRegistry types;
    MeshView smooth_view;
    MeshView flat_view;
    REQUIRE(smooth_view.Open(ByteSpan(smooth.resources[0].blob.Data(), smooth.resources[0].blob.Size()), types).Ok());
    REQUIRE(flat_view.Open(ByteSpan(flat.resources[0].blob.Data(), flat.resources[0].blob.Size()), types).Ok());
    CHECK(smooth_view.VertexCount() == 4);  // shared
    CHECK(flat_view.VertexCount() == 6);    // one per triangle corner
    CHECK_FALSE(GmSameBytes(smooth.resources[0].blob, flat.resources[0].blob));
}

TEST_CASE("gltf mesh: a sparse accessor's overrides are in the mesh", "[asset][gltf]")
{
    GltfWriter w;
    // POSITION: no buffer view (zeros) with the third element moved to (0, 2, 0) by a sparse override.
    const u8 sparse_indices[4] = {2, 0, 0, 0};
    const f32 sparse_values[3] = {0, 2, 0};
    const u32 iv = w.AddView(sparse_indices, 1);
    const u32 vv = w.AddView(sparse_values, sizeof(sparse_values));
    char extra[200];
    FormatTo(extra, sizeof(extra),
             ",\"sparse\":{\"count\":1,\"indices\":{\"bufferView\":%u,\"componentType\":5121},"
             "\"values\":{\"bufferView\":%u}}",
             iv, vv);
    const u32 pos = w.AddAccessor(-1, 5126, 3, "VEC3", extra);
    char json[200];
    FormatTo(json, sizeof(json), "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%u}}]}]", pos);
    w.AddRoot(json);
    ImportResult result;
    GltfError error;
    REQUIRE(GmImport(w.Glb(), &result, &error).Ok());
    TypeRegistry types;
    MeshView view;
    REQUIRE(view.Open(ByteSpan(GmMeshOf(result).blob.Data(), GmMeshOf(result).blob.Size()), types).Ok());
    CHECK(view.BoundsMax()[1] == 2.0f);
    CHECK(view.BoundsMax()[0] == 0.0f);
}

// glTF coverage Day 10: morph targets are imported (they were refused); drop_morph_targets still drops them.
TEST_CASE("gltf mesh: morph targets are imported, or dropped with a warning", "[asset][gltf]")
{
    GltfWriter w;
    const u16 indices[3] = {0, 1, 2};
    const u32 pos = GmAcc(&w, GmkTrianglePositions, 9, 5126, "VEC3");
    const u32 idx = GmAcc(&w, indices, 3, 5123, "SCALAR");
    char json[300];
    FormatTo(json, sizeof(json),
             "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%u},\"indices\":%u,"
             "\"targets\":[{\"POSITION\":%u}]}],\"weights\":[0]}]",
             pos, idx, pos);
    w.AddRoot(json);
    const Vector<byte> glb = w.Glb();
    ImportResult result;
    GltfError error;
    REQUIRE(GmImport(glb, &result, &error).Ok());
    REQUIRE(result.resources.Size() == 1);
    {
        const TypeRegistry types;
        MeshView view;
        REQUIRE(view.Open(ByteSpan(result.resources[0].blob.Data(), result.resources[0].blob.Size()), types).Ok());
        REQUIRE(view.MorphTargetCount() == 1);
        CHECK(StringView(view.MorphTarget(0).name.Data()) == StringView("target0"));
        CHECK(view.MorphTarget(0).delta_count == 2);  // the first vertex is at the origin: its delta is zero
    }
    ImportOptions options;
    options.drop_morph_targets = true;
    REQUIRE(GmImport(glb, &result, &error, options).Ok());
    CHECK(result.report.warnings == 2);  // the morph targets, and the texture coordinates the file lacks
    CHECK(result.resources.Size() == 1);
}

TEST_CASE("gltf mesh: other attributes are dropped with a counted warning", "[asset][gltf]")
{
    GltfWriter w;
    const u16 indices[3] = {0, 1, 2};
    const f32 colors[12] = {1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1};
    const u32 pos = GmAcc(&w, GmkTrianglePositions, 9, 5126, "VEC3");
    const u32 uv = GmAcc(&w, GmkTriangleUvs, 6, 5126, "VEC2");
    const u32 col = GmAcc(&w, colors, 12, 5126, "VEC4");
    const u32 idx = GmAcc(&w, indices, 3, 5123, "SCALAR");
    char json[300];
    FormatTo(json, sizeof(json),
             "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%u,\"TEXCOORD_0\":%u,\"TEXCOORD_1\":%u,"
             "\"COLOR_0\":%u},\"indices\":%u}]}]",
             pos, uv, uv, col, idx);
    w.AddRoot(json);
    ImportResult result;
    GltfError error;
    REQUIRE(GmImport(w.Glb(), &result, &error).Ok());
    CHECK(result.report.warnings == 2);  // TEXCOORD_1 and COLOR_0
}

TEST_CASE("gltf mesh: the scale multiplies the positions", "[asset][gltf]")
{
    ImportOptions options;
    options.scale = 0.01f;
    ImportResult result;
    GltfError error;
    REQUIRE(GmImport(GmTriangleGlb("t", true), &result, &error, options).Ok());
    TypeRegistry types;
    MeshView view;
    REQUIRE(view.Open(ByteSpan(GmMeshOf(result).blob.Data(), GmMeshOf(result).blob.Size()), types).Ok());
    CHECK(view.BoundsMax()[0] == 0.01f);
    options.scale = 0.0f;
    CHECK(GmImport(GmTriangleGlb("t", true), &result, &error, options).Is(ErrorCode::Invalid));
}

TEST_CASE("gltf mesh: a skinned primitive is skinned to its mesh's skeleton, or imported without the skin",
          "[asset][gltf]")
{
    GltfWriter w;
    const u16 indices[3] = {0, 1, 2};
    const u8 joints[12] = {0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0};
    const f32 weights[12] = {0.75f, 0.25f, 0, 0, 1, 0, 0, 0, 0.5f, 0.5f, 0, 0};
    const u32 pos = GmAcc(&w, GmkTrianglePositions, 9, 5126, "VEC3");
    const u32 jnt = GmAcc(&w, joints, 12, 5121, "VEC4");
    const u32 wgt = GmAcc(&w, weights, 12, 5126, "VEC4");
    const u32 idx = GmAcc(&w, indices, 3, 5123, "SCALAR");
    char json[300];
    FormatTo(json, sizeof(json),
             "\"meshes\":[{\"name\":\"skinned\",\"primitives\":[{\"attributes\":{\"POSITION\":%u,\"JOINTS_0\":%u,"
             "\"WEIGHTS_0\":%u},\"indices\":%u}]}]",
             pos, jnt, wgt, idx);
    w.AddRoot(json);
    const Vector<byte> glb = w.Glb();

    NoGltfSource none;
    GltfDocument document;
    GltfError error;
    REQUIRE(ParseGltf(ByteSpan(glb.Data(), glb.Size()), none, ImportLimits(), &document, &error).Ok());
    const TypeRegistry types;
    SeededIdSource ids(3);
    ImportOptions options;
    options.ids = &ids;

    MeshSkeletonBinding binding;
    binding.present = true;
    binding.skeleton = test::Rid(0xaa);
    binding.joint_count = 2;
    ImportResult skinned;
    REQUIRE(
        ImportMeshes(document, options, types, Span<const MeshSkeletonBinding>(&binding, 1), &skinned, &error).Ok());
    MeshView view;
    REQUIRE(view.Open(ByteSpan(skinned.resources[0].blob.Data(), skinned.resources[0].blob.Size()), types).Ok());
    CHECK(view.Skinned());
    CHECK(view.Skeleton() == test::Rid(0xaa));
    CHECK(view.SkinJointCount() == 2);
    CHECK(view.CheckSkin().Ok());
    const MeshVertex vertices[3] = {GmVertex(0, 0, 0, 0, 0, 1, 0, 0), GmVertex(1, 0, 0, 0, 0, 1, 0, 0),
                                    GmVertex(0, 1, 0, 0, 0, 1, 0, 0)};
    const u32 want_indices[3] = {0, 1, 2};
    MeshSkinInfluence skin[3];
    const u32 j0[2] = {0, 1};
    const f64 w0[2] = {0.75, 0.25};
    REQUIRE(QuantizeSkinInfluences(j0, w0, 2, &skin[0]));
    const u32 j1[1] = {1};
    const f64 w1[1] = {1.0};
    REQUIRE(QuantizeSkinInfluences(j1, w1, 1, &skin[1]));
    const u32 j2[2] = {0, 1};
    const f64 w2[2] = {0.5, 0.5};
    REQUIRE(QuantizeSkinInfluences(j2, w2, 2, &skin[2]));
    MeshSlot slot;
    slot.name = GmName("primitive0");
    slot.index_count = 3;
    MeshBuildInput input;
    input.quality = MeshQuality::Full;  // the importer writes the layout the G-buffer reads (Phase 24 Day 383)
    input.vertices = Span<const MeshVertex>(vertices, 3);
    input.indices = Span<const u32>(want_indices, 3);
    input.slots = Span<const MeshSlot>(&slot, 1);
    input.skin = Span<const MeshSkinInfluence>(skin, 3);
    input.skeleton = binding.skeleton;
    input.skeleton_joint_count = 2;
    Vector<byte> expected;
    REQUIRE(BuildMeshBlob(types, skinned.resources[0].id, input, &expected).Ok());
    CHECK(GmSameBytes(skinned.resources[0].blob, expected));

    // A joint past the skeleton is refused, naming the primitive.
    binding.joint_count = 1;
    CHECK(ImportMeshes(document, options, types, Span<const MeshSkeletonBinding>(&binding, 1), &skinned, &error)
              .Is(ErrorCode::Invalid));
    CHECK(GmSays(error, "meshes[0].primitives[0]"));

    // Without a skeleton the mesh imports unskinned, with a warning.
    ImportResult bare;
    REQUIRE(GmImport(glb, &bare, &error).Ok());
    REQUIRE(view.Open(ByteSpan(bare.resources[0].blob.Data(), bare.resources[0].blob.Size()), types).Ok());
    CHECK_FALSE(view.Skinned());
    CHECK(bare.report.warnings >= 1);
}

TEST_CASE("gltf mesh: more than four influences keep the four largest", "[asset][gltf]")
{
    GltfWriter w;
    const u16 indices[3] = {0, 1, 2};
    const u8 joints0[12] = {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3};
    const u8 joints1[12] = {4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7};
    const f32 weights0[12] = {0.05f, 0.1f, 0.15f, 0.2f, 0.05f, 0.1f, 0.15f, 0.2f, 0.05f, 0.1f, 0.15f, 0.2f};
    const f32 weights1[12] = {0.25f, 0.1f, 0.05f, 0.05f, 0.25f, 0.1f, 0.05f, 0.05f, 0.25f, 0.1f, 0.05f, 0.05f};
    const u32 pos = GmAcc(&w, GmkTrianglePositions, 9, 5126, "VEC3");
    const u32 j0 = GmAcc(&w, joints0, 12, 5121, "VEC4");
    const u32 j1 = GmAcc(&w, joints1, 12, 5121, "VEC4");
    const u32 w0 = GmAcc(&w, weights0, 12, 5126, "VEC4");
    const u32 w1 = GmAcc(&w, weights1, 12, 5126, "VEC4");
    const u32 idx = GmAcc(&w, indices, 3, 5123, "SCALAR");
    char json[400];
    FormatTo(json, sizeof(json),
             "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%u,\"JOINTS_0\":%u,\"WEIGHTS_0\":%u,"
             "\"JOINTS_1\":%u,\"WEIGHTS_1\":%u},\"indices\":%u}]}]",
             pos, j0, w0, j1, w1, idx);
    w.AddRoot(json);
    const Vector<byte> glb = w.Glb();
    NoGltfSource none;
    GltfDocument document;
    GltfError error;
    REQUIRE(ParseGltf(ByteSpan(glb.Data(), glb.Size()), none, ImportLimits(), &document, &error).Ok());
    const TypeRegistry types;
    MeshSkeletonBinding binding;
    binding.present = true;
    binding.skeleton = test::Rid(0xaa);
    binding.joint_count = 8;
    ImportResult result;
    REQUIRE(
        ImportMeshes(document, ImportOptions(), types, Span<const MeshSkeletonBinding>(&binding, 1), &result, &error)
            .Ok());
    CHECK(result.report.warnings >= 1);
    TypeRegistry registry;
    MeshView view;
    REQUIRE(view.Open(ByteSpan(GmMeshOf(result).blob.Data(), GmMeshOf(result).blob.Size()), registry).Ok());
    CHECK(view.CheckSkin().Ok());
}

TEST_CASE("gltf mesh: review findings: skin sets, scaling, slot names and warnings", "[asset][gltf]")
{
    const TypeRegistry types;
    // Skin sets with a gap (JOINTS_1 without JOINTS_0), and a joint past the skeleton on a vertex whose weight is zero.
    {
        GltfWriter w;
        const u16 indices[3] = {0, 1, 2};
        const u8 joints[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        const u8 far_joints[12] = {5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        const f32 weights[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
        const f32 zeros[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        const u32 pos = GmAcc(&w, GmkTrianglePositions, 9, 5126, "VEC3");
        const u32 j0 = GmAcc(&w, joints, 12, 5121, "VEC4");
        const u32 w0 = GmAcc(&w, weights, 12, 5126, "VEC4");
        const u32 jf = GmAcc(&w, far_joints, 12, 5121, "VEC4");
        const u32 wz = GmAcc(&w, zeros, 12, 5126, "VEC4");
        const u32 idx = GmAcc(&w, indices, 3, 5123, "SCALAR");
        char json[500];
        FormatTo(json, sizeof(json),
                 "\"meshes\":[{\"name\":\"gap\",\"primitives\":[{\"attributes\":{\"POSITION\":%u,\"JOINTS_1\":%u,"
                 "\"WEIGHTS_1\":%u},\"indices\":%u}]},"
                 "{\"name\":\"far\",\"primitives\":[{\"attributes\":{\"POSITION\":%u,\"JOINTS_0\":%u,"
                 "\"WEIGHTS_0\":%u,\"JOINTS_1\":%u,\"WEIGHTS_1\":%u},\"indices\":%u}]}]",
                 pos, j0, w0, idx, pos, j0, w0, jf, wz, idx);
        w.AddRoot(json);
        const Vector<byte> glb = w.Glb();
        NoGltfSource none;
        GltfDocument document;
        GltfError error;
        REQUIRE(ParseGltf(ByteSpan(glb.Data(), glb.Size()), none, ImportLimits(), &document, &error).Ok());
        MeshSkeletonBinding bindings[2];
        for (MeshSkeletonBinding& b : bindings)
        {
            b.present = true;
            b.skeleton = test::Rid(0xaa);
            b.joint_count = 2;
        }
        ImportResult result;
        CHECK(ImportMeshes(document, ImportOptions(), types, Span<const MeshSkeletonBinding>(bindings, 2), &result,
                           &error)
                  .Is(ErrorCode::Invalid));
        CHECK(GmSays(error, "meshes[0].primitives[0].attributes"));
        CHECK(GmSays(error, "without a gap"));
    }
    // A second set that is all zeros keeps every influence: no "more than four" warning; the joint past the skeleton on
    // a zero weight is still refused.
    {
        GltfWriter w;
        const u16 indices[3] = {0, 1, 2};
        const u8 joints[12] = {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0};
        const u8 more[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        const f32 weights[12] = {0.5f, 0.5f, 0, 0, 0.5f, 0.5f, 0, 0, 0.5f, 0.5f, 0, 0};
        const f32 zeros[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        const u32 pos = GmAcc(&w, GmkTrianglePositions, 9, 5126, "VEC3");
        const u32 j0 = GmAcc(&w, joints, 12, 5121, "VEC4");
        const u32 w0 = GmAcc(&w, weights, 12, 5126, "VEC4");
        const u32 j1 = GmAcc(&w, more, 12, 5121, "VEC4");
        const u32 w1 = GmAcc(&w, zeros, 12, 5126, "VEC4");
        const u32 idx = GmAcc(&w, indices, 3, 5123, "SCALAR");
        char json[400];
        FormatTo(json, sizeof(json),
                 "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%u,\"JOINTS_0\":%u,\"WEIGHTS_0\":%u,"
                 "\"JOINTS_1\":%u,\"WEIGHTS_1\":%u},\"indices\":%u}]}]",
                 pos, j0, w0, j1, w1, idx);
        w.AddRoot(json);
        const Vector<byte> glb = w.Glb();
        NoGltfSource none;
        GltfDocument document;
        GltfError error;
        REQUIRE(ParseGltf(ByteSpan(glb.Data(), glb.Size()), none, ImportLimits(), &document, &error).Ok());
        MeshSkeletonBinding binding;
        binding.present = true;
        binding.skeleton = test::Rid(0xaa);
        binding.joint_count = 2;
        ImportResult result;
        REQUIRE(ImportMeshes(document, ImportOptions(), types, Span<const MeshSkeletonBinding>(&binding, 1), &result,
                             &error)
                    .Ok());
        CHECK(result.report.warnings == 1);  // only the missing texture coordinates
    }
    // A scale that takes a position out of the float range is refused, naming the accessor reference.
    {
        GltfWriter w;
        const f32 huge[9] = {0, 0, 0, 1e30f, 0, 0, 0, 1, 0};
        const u16 indices[3] = {0, 1, 2};
        const u32 pos = GmAcc(&w, huge, 9, 5126, "VEC3");
        const u32 idx = GmAcc(&w, indices, 3, 5123, "SCALAR");
        char json[200];
        FormatTo(json, sizeof(json),
                 "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%u},\"indices\":%u}]}]", pos, idx);
        w.AddRoot(json);
        ImportOptions options;
        options.scale = 1e12f;
        ImportResult result;
        GltfError error;
        CHECK(GmImport(w.Glb(), &result, &error, options).Is(ErrorCode::Invalid));
        CHECK(GmSays(error, "meshes[0].primitives[0].attributes.POSITION"));
    }
    // Materials named like the generated slot names still give distinct slots.
    {
        GltfWriter w;
        const u16 indices[3] = {0, 1, 2};
        const u32 pos = GmAcc(&w, GmkTrianglePositions, 9, 5126, "VEC3");
        const u32 idx = GmAcc(&w, indices, 3, 5123, "SCALAR");
        char prim[3][120];
        for (u32 i = 0; i < 3; ++i)
        {
            FormatTo(prim[i], sizeof(prim[i]), "{\"attributes\":{\"POSITION\":%u},\"indices\":%u,\"material\":%u}", pos,
                     idx, i);
        }
        char json[700];
        FormatTo(json, sizeof(json),
                 "\"materials\":[{\"name\":\"x\"},{\"name\":\"primitive2\"},{\"name\":\"primitive2\"}],"
                 "\"meshes\":[{\"primitives\":[%s,%s,%s,%s]}]",
                 prim[0], prim[1], prim[2], prim[1]);
        w.AddRoot(json);
        ImportResult result;
        GltfError error;
        REQUIRE(GmImport(w.Glb(), &result, &error).Ok());
        TypeRegistry registry;
        MeshView view;
        REQUIRE(view.Open(ByteSpan(GmMeshOf(result).blob.Data(), GmMeshOf(result).blob.Size()), registry).Ok());
        REQUIRE(view.SlotCount() == 4);
        for (u32 i = 0; i < 4; ++i)
        {
            for (u32 k = i + 1; k < 4; ++k)
            {
                CHECK(StringView(view.Slot(i).name.Data()) != StringView(view.Slot(k).name.Data()));
            }
        }
    }
    // Materials are imported as well (Day 338): the mesh's own is among the resources.
    {
        GltfWriter w;
        const u16 indices[3] = {0, 1, 2};
        const u32 pos = GmAcc(&w, GmkTrianglePositions, 9, 5126, "VEC3");
        const u32 idx = GmAcc(&w, indices, 3, 5123, "SCALAR");
        char json[300];
        FormatTo(json, sizeof(json),
                 "\"materials\":[{}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%u},"
                 "\"indices\":%u}]}]",
                 pos, idx);
        w.AddRoot(json);
        ImportResult result;
        GltfError error;
        REQUIRE(GmImport(w.Glb(), &result, &error).Ok());
        bool has_material = false;
        for (usize i = 0; i < result.resources.Size(); ++i)
        {
            has_material = has_material || result.resources[i].type == types::kMaterial;
        }
        CHECK(has_material);
    }
}

TEST_CASE("gltf mesh: a JOINTS accessor of the wrong size is refused before it is read", "[asset][gltf]")
{
    GltfWriter w;
    const u16 indices[3] = {0, 1, 2};
    const u8 joints[40] = {};
    const f32 weights[40] = {};
    const u32 pos = GmAcc(&w, GmkTrianglePositions, 9, 5126, "VEC3");
    const u32 j = GmAcc(&w, joints, 40, 5121, "VEC4");
    const u32 wt = GmAcc(&w, weights, 40, 5126, "VEC4");
    const u32 idx = GmAcc(&w, indices, 3, 5123, "SCALAR");
    char json[300];
    FormatTo(json, sizeof(json),
             "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%u,\"JOINTS_0\":%u,\"WEIGHTS_0\":%u},"
             "\"indices\":%u}]}]",
             pos, j, wt, idx);
    w.AddRoot(json);
    ImportResult result;
    GltfError error;
    CHECK(GmImport(w.Glb(), &result, &error).Is(ErrorCode::Invalid));
    CHECK(GmSays(error, "meshes[0].primitives[0].attributes.JOINTS_0"));
    CHECK(GmSays(error, "different number of elements"));
}

TEST_CASE("gltf mesh: hostile meshes are refused, naming the mesh and primitive", "[asset][gltf]")
{
    struct Case
    {
        const char* primitive;  // the primitive's JSON members, after the attributes
        const char* attributes;
        const char* says;
        ErrorCode code;
    };
    // Accessors: 0 positions (3 vec3), 1 u16 indices (3), 2 u16 indices past the vertices, 3 vec3 (2 elements),
    // 4 positions with a NaN, 5 u16 indices (2).
    const Case cases[] = {
        {",\"mode\":0", "\"POSITION\":0", "meshes[0].primitives[0]", ErrorCode::Unsupported},
        {",\"mode\":1", "\"POSITION\":0", "only triangles", ErrorCode::Unsupported},
        {",\"indices\":1", "\"NORMAL\":0", "no POSITION", ErrorCode::Invalid},
        {",\"indices\":2", "\"POSITION\":0", "index past the vertices", ErrorCode::Invalid},
        {",\"indices\":1", "\"POSITION\":0,\"NORMAL\":3", "different number of elements", ErrorCode::Invalid},
        {",\"indices\":1", "\"POSITION\":4", "accessors[4]", ErrorCode::Invalid},
        {",\"indices\":5,\"mode\":5", "\"POSITION\":0", "at least three indices", ErrorCode::Invalid},
        {",\"indices\":5", "\"POSITION\":0", "multiple of three", ErrorCode::Invalid},
        {",\"indices\":1,\"material\":4", "\"POSITION\":0", "primitives[0].material", ErrorCode::Invalid},
        {",\"indices\":1", "\"POSITION\":9", "primitives[0].attributes.POSITION", ErrorCode::Invalid},
        {",\"indices\":1", "\"POSITION\":1", "wrong element type", ErrorCode::Invalid},
    };
    for (const Case& c : cases)
    {
        GltfWriter w;
        const u16 indices[3] = {0, 1, 2};
        const u16 past[3] = {0, 1, 3};
        const f32 two[6] = {0, 0, 0, 1, 0, 0};
        const f32 nan_bits[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        u32 nan_word = 0x7fc00000u;
        f32 nan_value;
        MemCopy(&nan_value, &nan_word, 4);
        f32 with_nan[9];
        MemCopy(with_nan, nan_bits, sizeof(with_nan));
        with_nan[4] = nan_value;
        const u16 pair[2] = {0, 1};
        GmAcc(&w, GmkTrianglePositions, 9, 5126, "VEC3");
        GmAcc(&w, indices, 3, 5123, "SCALAR");
        GmAcc(&w, past, 3, 5123, "SCALAR");
        GmAcc(&w, two, 6, 5126, "VEC3");
        GmAcc(&w, with_nan, 9, 5126, "VEC3");
        GmAcc(&w, pair, 2, 5123, "SCALAR");
        char json[400];
        FormatTo(json, sizeof(json), "\"meshes\":[{\"name\":\"bad\",\"primitives\":[{\"attributes\":{%s}%s}]}]",
                 c.attributes, c.primitive);
        w.AddRoot(json);
        ImportResult result;
        GltfError error;
        const Vector<byte> glb = w.Glb();
        NoGltfSource none;
        SeededIdSource ids(1);
        ImportOptions options;
        options.ids = &ids;
        const Status status = ImportGltf(ByteSpan(glb.Data(), glb.Size()), none, options, &result, &error);
        INFO(c.attributes << c.primitive);
        INFO(GltfMessage(error, status));
        CHECK(status.Failed());
        CHECK(status.Is(c.code));
        CHECK(GmSays(error, c.says));
        CHECK(result.resources.Size() == 0);
    }
}

TEST_CASE("gltf mesh: a mesh with no primitives or a NaN position is refused", "[asset][gltf]")
{
    GltfWriter w;
    u32 nan_word = 0x7fc00000u;
    f32 nan_value;
    MemCopy(&nan_value, &nan_word, 4);
    const f32 points[9] = {0, 0, 0, nan_value, 0, 0, 0, 1, 0};
    GmAcc(&w, points, 9, 5126, "VEC3");
    w.AddRoot("\"meshes\":[{\"name\":\"empty\",\"primitives\":[]}]");
    ImportResult result;
    GltfError error;
    CHECK(GmImport(w.Glb(), &result, &error).Is(ErrorCode::Invalid));
    CHECK(GmSays(error, "meshes[0]"));
    GltfWriter n;
    const u16 indices[3] = {0, 1, 2};
    const u32 pos = GmAcc(&n, points, 9, 5126, "VEC3");
    const u32 idx = GmAcc(&n, indices, 3, 5123, "SCALAR");
    char json[200];
    FormatTo(json, sizeof(json), "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%u},\"indices\":%u}]}]",
             pos, idx);
    n.AddRoot(json);
    CHECK(GmImport(n.Glb(), &result, &error).Failed());
}

TEST_CASE("gltf mesh: an import is deterministic and keeps its ids when the meshes are reordered", "[asset][gltf]")
{
    auto build = [](bool swapped)
    {
        GltfWriter w;
        const u16 indices[3] = {0, 1, 2};
        const f32 other[9] = {0, 0, 0, 2, 0, 0, 0, 2, 0};
        const u32 a = GmAcc(&w, GmkTrianglePositions, 9, 5126, "VEC3");
        const u32 b = GmAcc(&w, other, 9, 5126, "VEC3");
        const u32 idx = GmAcc(&w, indices, 3, 5123, "SCALAR");
        char first[160];
        char second[160];
        FormatTo(first, sizeof(first),
                 "{\"name\":\"alpha\",\"primitives\":[{\"attributes\":{\"POSITION\":%u},\"indices\":%u}]}", a, idx);
        FormatTo(second, sizeof(second),
                 "{\"name\":\"beta\",\"primitives\":[{\"attributes\":{\"POSITION\":%u},\"indices\":%u}]}", b, idx);
        char json[400];
        FormatTo(json, sizeof(json), "\"meshes\":[%s,%s]", swapped ? second : first, swapped ? first : second);
        w.AddRoot(json);
        return w.Glb();
    };
    const Vector<byte> original = build(false);
    ImportResult one;
    ImportResult two;
    GltfError error;
    REQUIRE(GmImport(original, &one, &error).Ok());
    REQUIRE(GmImport(original, &two, &error).Ok());
    REQUIRE(one.resources.Size() == 2);
    for (usize i = 0; i < 2; ++i)
    {
        CHECK(one.resources[i].id == two.resources[i].id);
        CHECK(GmSameBytes(one.resources[i].blob, two.resources[i].blob));
    }
    String<HeapAllocator> text_one(HeapAllocator::Default());
    String<HeapAllocator> text_two(HeapAllocator::Default());
    one.manifest.Format(&text_one);
    two.manifest.Format(&text_two);
    CHECK(text_one.View() == text_two.View());

    // The same file with its meshes swapped, imported with the first import's manifest: the ids follow the names.
    ImportOptions options;
    options.manifest = &one.manifest;
    SeededIdSource fresh(99);
    options.ids = &fresh;
    ImportResult swapped;
    NoGltfSource none;
    const Vector<byte> reordered = build(true);
    REQUIRE(ImportGltf(ByteSpan(reordered.Data(), reordered.Size()), none, options, &swapped, &error).Ok());
    REQUIRE(swapped.resources.Size() == 2);
    CHECK(swapped.resources[0].path.View() == StringView("/gltf/meshes/beta"));
    CHECK(swapped.resources[0].id == one.resources[1].id);
    CHECK(swapped.resources[1].id == one.resources[0].id);
}

TEST_CASE("gltf mesh: names that clash or hold a slash become distinct path segments", "[asset][gltf]")
{
    GltfWriter w;
    const u16 indices[3] = {0, 1, 2};
    const u32 pos = GmAcc(&w, GmkTrianglePositions, 9, 5126, "VEC3");
    const u32 idx = GmAcc(&w, indices, 3, 5123, "SCALAR");
    char prim[100];
    FormatTo(prim, sizeof(prim), "{\"attributes\":{\"POSITION\":%u},\"indices\":%u}", pos, idx);
    char json[700];
    FormatTo(json, sizeof(json),
             "\"meshes\":[{\"name\":\"a\",\"primitives\":[%s]},{\"name\":\"a\",\"primitives\":[%s]},"
             "{\"name\":\"a_1\",\"primitives\":[%s]},{\"name\":\"x/y\",\"primitives\":[%s]},"
             "{\"name\":\"..\",\"primitives\":[%s]},{\"primitives\":[%s]}]",
             prim, prim, prim, prim, prim, prim);
    w.AddRoot(json);
    ImportResult result;
    GltfError error;
    REQUIRE(GmImport(w.Glb(), &result, &error).Ok());
    REQUIRE(result.resources.Size() == 6);
    for (usize i = 0; i < 6; ++i)
    {
        for (usize k = i + 1; k < 6; ++k)
        {
            CHECK(result.resources[i].path.View() != result.resources[k].path.View());
        }
    }
    CHECK(result.resources[3].path.View() == StringView("/gltf/meshes/x_y"));
    CHECK(result.resources[4].path.View() == StringView("/gltf/meshes/_"));
    CHECK(result.resources[5].path.View() == StringView("/gltf/meshes/mesh5"));
    TypeRegistry types;
    HostMemoryBackend backend(types, u64{16} << 20);
    CHECK(ImportIntoBackend(backend, result).Ok());
}

TEST_CASE("gltf mesh: imported meshes load through the resource manager from a memory backend", "[asset][gltf]")
{
    ImportResult result;
    GltfError error;
    REQUIRE(GmImport(GmTriangleGlb("tri", true), &result, &error).Ok());
    TypeRegistry types;
    HostMemoryBackend backend(types, u64{16} << 20);
    REQUIRE(ImportIntoBackend(backend, result).Ok());
    ManagerConfig config;
    config.buffer_pool_bytes = u64{16} << 20;
    ResourceManager manager(types, config);
    RegisterCpuLoaders(&manager);
    REQUIRE(manager.Mount(StringView("/"), &backend, 0).Ok());

    REQUIRE(manager.Mounts().RefreshTables().Ok());  // the tables a path is resolved through
    Handle<MeshView> by_path;
    const Status loaded = manager.LoadPath<MeshView>(StringView("/gltf/meshes/tri"), &by_path);
    INFO(loaded.Message());
    REQUIRE(loaded.Ok());
    REQUIRE(by_path.Get() != nullptr);
    CHECK(by_path.Get()->VertexCount() == 3);
    CHECK(by_path.Get()->IndexCount() == 3);
    CHECK(by_path.Get()->CheckIndices().Ok());
    Handle<MeshView> by_id;
    REQUIRE(manager.Load<MeshView>(result.resources[0].id, &by_id).Ok());
    CHECK(by_id.Get() == by_path.Get());  // one loaded object

    // The asynchronous path.
    by_id.Reset();
    by_path.Reset();
    jobs::Scheduler::Config scheduler_config;
    scheduler_config.participants = 3;
    scheduler_config.pin = false;
    jobs::Scheduler scheduler(scheduler_config);
    AsyncLoader loader(manager, scheduler);
    LoadTicket ticket = loader.LoadPathAsync<MeshView>(StringView("/gltf/meshes/tri"));
    REQUIRE(ticket.Valid());
    CHECK(loader.Wait(ticket).Ok());
    Handle<MeshView> async;
    REQUIRE(ticket.Finish(&async).Ok());
    CHECK(async.Get()->VertexCount() == 3);

    // A re-import with the first import's manifest replaces the blob under the same id, and the manager reloads it.
    GltfWriter w;
    const f32 moved[9] = {0, 0, 0, 4, 0, 0, 0, 4, 0};
    const u16 indices[3] = {0, 1, 2};
    const u32 pos = GmAcc(&w, moved, 9, 5126, "VEC3");
    const u32 idx = GmAcc(&w, indices, 3, 5123, "SCALAR");
    char json[300];
    FormatTo(json, sizeof(json),
             "\"meshes\":[{\"name\":\"tri\",\"primitives\":[{\"attributes\":{\"POSITION\":%u},\"indices\":%u}]}]", pos,
             idx);
    w.AddRoot(json);
    ImportOptions options;
    options.manifest = &result.manifest;
    ImportResult again;
    REQUIRE(GmImport(w.Glb(), &again, &error, options).Ok());
    REQUIRE(again.resources.Size() == 1);
    CHECK(again.resources[0].id == result.resources[0].id);
    Vector<ReloadResult> reloads;
    REQUIRE(manager.PollChanges(&reloads).Ok());  // the adds
    REQUIRE(
        backend.Replace(again.resources[0].id, ByteSpan(again.resources[0].blob.Data(), again.resources[0].blob.Size()))
            .Ok());
    REQUIRE(manager.PollChanges(&reloads).Ok());
    REQUIRE(reloads.Size() == 1);
    CHECK(reloads[0].status.Ok());
    CHECK(async.Get()->BoundsMax()[0] == 4.0f);
}

// Maintenance 2026-11-18: the summary a host stores beside the cached blobs (asset_import/gltf_summary.h).
TEST_CASE("The import summary names the nodes, the bounds, the materials and the animations",
          "[asset][gltf][level][summary]")
{
    GltfWriter w;
    const f32 positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const u16 indices[3] = {0, 1, 2};
    const f32 times[2] = {0, 2.5f};
    const f32 values[6] = {0, 0, 0, 0, 3, 0};
    const u32 pos = GmAcc(&w, positions, 9, 5126, "VEC3");
    const u32 idx = GmAcc(&w, indices, 3, 5123, "SCALAR");
    const u32 time = GmAcc(&w, times, 2, 5126, "SCALAR");
    const u32 translation = GmAcc(&w, values, 6, 5126, "VEC3");
    char json[1400];
    FormatTo(
        json, sizeof(json),
        "\"materials\":[{\"name\":\"Screen\"},{}],"
        "\"meshes\":[{\"name\":\"m\",\"primitives\":[{\"attributes\":{\"POSITION\":%u},\"indices\":%u}]}],"
        "\"nodes\":[{\"name\":\"Spawn\",\"translation\":[10,0,5]},"
        "{\"name\":\"thing\",\"mesh\":0,\"translation\":[1,2,3]},"
        "{\"name\":\"kid\",\"translation\":[0,0,1]},{\"name\":\"parent\",\"translation\":[5,0,0],\"children\":[2]}],"
        "\"scenes\":[{\"nodes\":[0,1,3]}],\"scene\":0,"
        "\"animations\":[{\"name\":\"Walk\",\"samplers\":[{\"input\":%u,\"output\":%u}],"
        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}}]}]",
        pos, idx, time, translation);
    w.AddRoot(json);
    ImportOptions options;
    options.emit_level = true;
    ImportResult result;
    GltfError error;
    REQUIRE(GmImport(w.Glb(), &result, &error, options).Ok());
    const StringView summary = result.summary.View();
    INFO(result.summary.CStr());
    CHECK(summary.Contains(StringView("\"version\":1")));
    CHECK(summary.Contains(StringView("{\"name\":\"Spawn\",\"position\":[10,0,5],\"mesh\":-1}")));
    CHECK(summary.Contains(StringView("{\"name\":\"thing\",\"position\":[1,2,3],\"mesh\":0}")));
    CHECK(summary.Contains(StringView("{\"name\":\"kid\",\"position\":[5,0,1],\"mesh\":-1}")));  // under its parent
    CHECK(summary.Contains(StringView("\"bounds\":{\"min\":[1,2,3],\"max\":[2,3,3]}")));
    CHECK(summary.Contains(StringView("\"materials\":[\"Screen\",\"\"]")));
    CHECK(summary.Contains(StringView("\"animations\":[{\"name\":\"Walk\",\"duration\":2.5}]")));

    // The import's `scale` applies to positions and bounds, not to durations.
    options.scale = 2.0f;
    ImportResult scaled;
    REQUIRE(GmImport(w.Glb(), &scaled, &error, options).Ok());
    const StringView big = scaled.summary.View();
    CHECK(big.Contains(StringView("\"position\":[20,0,10]")));
    CHECK(big.Contains(StringView("\"bounds\":{\"min\":[2,4,6],\"max\":[4,6,6]}")));
    CHECK(big.Contains(StringView("\"duration\":2.5")));

    // A file with no level has no summary.
    ImportOptions plain;
    ImportResult none;
    REQUIRE(GmImport(w.Glb(), &none, &error, plain).Ok());
    CHECK(none.summary.Length() == 0);
}

// Phase 24 Day 376: a node's level keeps the material of every primitive.
TEST_CASE("A level of a two-primitive mesh names each slot's material", "[asset][gltf][level][slots]")
{
    GltfWriter w;
    const f32 positions[18] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 1, 0, 1, 1};
    const u16 first[3] = {0, 1, 2};
    const u16 second[3] = {3, 4, 5};
    const u32 pos = GmAcc(&w, positions, 18, 5126, "VEC3");
    const u32 idx0 = GmAcc(&w, first, 3, 5123, "SCALAR");
    const u32 idx1 = GmAcc(&w, second, 3, 5123, "SCALAR");
    char json[700];
    FormatTo(json, sizeof(json),
             "\"materials\":[{\"name\":\"red\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0,0,1]}},"
             "{\"name\":\"blue\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0,0,1,1]}}],"
             "\"meshes\":[{\"name\":\"two\",\"primitives\":[{\"attributes\":{\"POSITION\":%u},\"indices\":%u,"
             "\"material\":0},{\"attributes\":{\"POSITION\":%u},\"indices\":%u,\"material\":1}]}],"
             "\"nodes\":[{\"name\":\"thing\",\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0",
             pos, idx0, pos, idx1);
    w.AddRoot(json);
    ImportOptions options;
    options.emit_level = true;
    ImportResult result;
    GltfError error;
    REQUIRE(GmImport(w.Glb(), &result, &error, options).Ok());
    const ImportedResource* level = nullptr;
    ResourceId red;
    ResourceId blue;
    for (usize i = 0; i < result.manifest.Entries().Size(); ++i)
    {
        const ImportManifestEntry& e = result.manifest.Entries()[i];
        if (e.kind == ImportKind::Material)
        {
            (e.index == 0 ? red : blue) = e.id;
        }
    }
    for (usize i = 0; i < result.resources.Size(); ++i)
    {
        if (result.resources[i].type == types::kLevel)
        {
            level = &result.resources[i];
        }
    }
    REQUIRE(level != nullptr);
    REQUIRE((!red.IsNone() && !blue.IsNone()));

    TypeRegistry types = test::MakeBackendRegistry();
    storage::ComponentRegistry registry;
    REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
    REQUIRE(RegisterLevelComponents(&registry).Ok());
    REQUIRE(animation::RegisterAnimationComponents(&registry).Ok());
    ecs::World world;
    storage::KeyIndex index;
    REQUIRE(LoadLevelBlob(ByteSpan(level->blob.Data(), level->blob.Size()), types, registry, world, index).Ok());
    u32 found = 0;
    world.Query<ecs::Read<MeshReference>, ecs::Read<MeshSlotMaterials>>().ForEach(
        [&](ecs::Entity, const MeshReference& reference, const MeshSlotMaterials& slots)
        {
            ++found;
            CHECK(reference.material.id == red.Value());
            CHECK(slots.At(0).id == red.Value());
            CHECK(slots.At(1).id == blue.Value());
            CHECK(slots.At(2).id == 0);
        });
    CHECK(found == 1);
}

// glTF coverage Day 12: KHR_lights_punctual lights become light_source components on their nodes' entities, in the
// engine's units (a point or spot light's intensity times the light scale, a sun's times the sun scale, a missing range
// from the cutoff), and a light that breaks a rule is refused.
namespace
{

bool GmNear(f32 a, f32 b)
{
    return Abs(a - b) <= 1e-5f * (1.0f + Abs(b));
}

Vector<byte> GmLightsGlb(const char* lights)
{
    GltfWriter w;
    const u16 indices[3] = {0, 1, 2};
    const u32 pos = GmAcc(&w, GmkTrianglePositions, 9, 5126, "VEC3");
    const u32 idx = GmAcc(&w, indices, 3, 5123, "SCALAR");
    char json[1400];
    FormatTo(json, sizeof(json),
             "\"extensionsUsed\":[\"KHR_lights_punctual\"],"
             "\"extensions\":{\"KHR_lights_punctual\":{\"lights\":[%s]}},"
             "\"meshes\":[{\"name\":\"tri\",\"primitives\":[{\"attributes\":{\"POSITION\":%u},\"indices\":%u}]}],"
             "\"nodes\":[{\"name\":\"thing\",\"mesh\":0},"
             "{\"name\":\"sun\",\"extensions\":{\"KHR_lights_punctual\":{\"light\":0}}},"
             "{\"name\":\"bulb\",\"extensions\":{\"KHR_lights_punctual\":{\"light\":1}}},"
             "{\"name\":\"torch\",\"extensions\":{\"KHR_lights_punctual\":{\"light\":2}}}],"
             "\"scenes\":[{\"nodes\":[0,1,2,3]}],\"scene\":0",
             lights, pos, idx);
    w.AddRoot(json);
    return w.Glb();
}

}  // namespace

TEST_CASE("gltf: KHR_lights_punctual lights land on their nodes as light sources, in the engine's units",
          "[asset][gltf][level][lights]")
{
    const char* lights = "{\"type\":\"directional\",\"color\":[1,0.5,0.25],\"intensity\":20000},"
                         "{\"type\":\"point\",\"intensity\":3,\"range\":12},"
                         "{\"type\":\"spot\",\"intensity\":1,\"spot\":{\"innerConeAngle\":0.2,\"outerConeAngle\":0.6}}";
    ImportOptions options;
    options.emit_level = true;
    options.light_intensity_scale = 2.0f;
    options.sun_intensity_scale = 1e-4f;
    options.light_cutoff = 0.25f;
    ImportResult result;
    GltfError error;
    REQUIRE(GmImport(GmLightsGlb(lights), &result, &error, options).Ok());
    const ImportedResource* level = nullptr;
    for (usize i = 0; i < result.resources.Size(); ++i)
    {
        if (result.resources[i].type == types::kLevel)
        {
            level = &result.resources[i];
        }
    }
    REQUIRE(level != nullptr);
    TypeRegistry types = test::MakeBackendRegistry();
    storage::ComponentRegistry registry;
    REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
    REQUIRE(RegisterLevelComponents(&registry).Ok());
    REQUIRE(animation::RegisterAnimationComponents(&registry).Ok());
    REQUIRE(scene::RegisterSceneComponents(&registry).Ok());
    ecs::World world;
    storage::KeyIndex index;
    REQUIRE(LoadLevelBlob(ByteSpan(level->blob.Data(), level->blob.Size()), types, registry, world, index).Ok());
    u32 found = 0;
    world.Query<ecs::Read<scene::LightSource>, ecs::Read<EntityName>>().ForEach(
        [&](ecs::Entity, const scene::LightSource& light, const EntityName& name)
        {
            ++found;
            CHECK(light.direction.z == 1.0f);  // glTF lights shine along -Z
            if (name.name.View() == StringView("sun"))
            {
                CHECK(light.type == renderer::LightType::Directional);
                CHECK(GmNear(light.intensity, 2.0f));
                CHECK(light.color.y == 0.5f);
                CHECK(light.color.z == 0.25f);
            }
            else if (name.name.View() == StringView("bulb"))
            {
                CHECK(light.type == renderer::LightType::Point);
                CHECK(GmNear(light.intensity, 6.0f));
                CHECK(GmNear(light.radius, 12.0f));
            }
            else
            {
                CHECK(light.type == renderer::LightType::Spot);
                CHECK(GmNear(light.intensity, 2.0f));
                CHECK(GmNear(light.radius, Sqrt(2.0f / 0.25f - 1.0f)));  // no range: intensity / (d^2 + 1) = cutoff
                CHECK(GmNear(light.inner_cone, 0.2f));
                CHECK(GmNear(light.outer_cone, 0.6f));
            }
        });
    CHECK(found == 3);

    // The same file imports to the same level bytes.
    ImportResult again;
    REQUIRE(GmImport(GmLightsGlb(lights), &again, &error, options).Ok());
    bool same = false;
    for (usize i = 0; i < again.resources.Size(); ++i)
    {
        if (again.resources[i].type == types::kLevel)
        {
            same = again.resources[i].blob.Size() == level->blob.Size() &&
                   MemCompare(again.resources[i].blob.Data(), level->blob.Data(), level->blob.Size()) == 0;
        }
    }
    CHECK(same);

    // Lights that break a rule are refused, naming the light.
    const char* bad[] = {
        "{\"type\":\"area\"},{\"type\":\"point\"},{\"type\":\"point\"}",
        "{\"type\":\"point\",\"intensity\":-1},{\"type\":\"point\"},{\"type\":\"point\"}",
        "{\"type\":\"point\",\"range\":0},{\"type\":\"point\"},{\"type\":\"point\"}",
        ("{\"type\":\"spot\",\"spot\":{\"innerConeAngle\":0.6,\"outerConeAngle\":0.5}},{\"type\":\"point\"},"
         "{\"type\":\"point\"}"),  // one element, split for the line length
        "{\"type\":\"spot\",\"spot\":{\"outerConeAngle\":2}},{\"type\":\"point\"},{\"type\":\"point\"}",
        "{\"type\":\"point\"},{\"type\":\"point\"}",  // node 3 names light 2, which is not there
    };
    for (const char* b : bad)
    {
        INFO(b);
        ImportResult refused;
        CHECK(GmImport(GmLightsGlb(b), &refused, &error, options).Failed());
    }
}

}  // namespace engine_placeholder::asset
