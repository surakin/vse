// The mesh format, its source form and the minter (Phase 22 Day 319).

#include <catch2/catch_test_macros.hpp>

#include "asset/mesh.h"
#include "containers/array.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "lang/limits.h"
#include "lang/scalar.h"
#include "mesh_source.h"
#include "skeleton_source.h"
#include "support.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr const char* kMshId = "id 00000000000000bb\n";
constexpr const char* kMshSkelId = "00000000000000aa";

void MshAppend(Vector<char>* out, const char* text)
{
    for (; *text != '\0'; ++text)
    {
        out->Push(*text);
    }
}

StringView MshView(const Vector<char>& v)
{
    return StringView(v.Data(), v.Size());
}

// A (n+1) x (n+1) grid of vertices in the xy plane, x and y in [0, n], uv = (x/n, y/n) scaled by `uv_scale`, two
// triangles per cell.
Vector<char> MshGrid(u32 n, const char* header_lines = "", f32 uv_scale = 1.0f)
{
    Vector<char> text;
    MshAppend(&text, kMshId);
    MshAppend(&text, header_lines);
    for (u32 y = 0; y <= n; ++y)
    {
        for (u32 x = 0; x <= n; ++x)
        {
            char line[200];
            FormatTo(line, sizeof(line), "v %u %u 0  0 0 1  1 0 0 1  %f %f\n", x, y,
                     static_cast<double>(uv_scale * static_cast<f32>(x) / static_cast<f32>(n)),
                     static_cast<double>(uv_scale * static_cast<f32>(y) / static_cast<f32>(n)));
            MshAppend(&text, line);
        }
    }
    for (u32 y = 0; y < n; ++y)
    {
        for (u32 x = 0; x < n; ++x)
        {
            const u32 a = y * (n + 1) + x;
            char line[80];
            FormatTo(line, sizeof(line), "i %u %u %u\ni %u %u %u\n", a, a + 1, a + n + 1, a + 1, a + n + 2, a + n + 1);
            MshAppend(&text, line);
        }
    }
    return text;
}

Vector<byte> MshMint(const TypeRegistry& types, const Vector<char>& text, const SkeletonView* skeleton = nullptr)
{
    Vector<byte> blob;
    MeshSourceError error;
    const Status s = MintMesh(MshView(text), types, skeleton, &blob, &error);
    INFO(error.message.Data());
    REQUIRE(s.Ok());
    return blob;
}

Vector<byte> MshCopy(const Vector<byte>& bytes)
{
    Vector<byte> out;
    out.Resize(bytes.Size());
    MemCopy(out.Data(), bytes.Data(), bytes.Size());
    return out;
}

Vector<byte> MshSection(const TypeRegistry& types, const Vector<byte>& blob, u32 id)
{
    BlobReader reader;
    REQUIRE(reader.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    ByteSpan data;
    REQUIRE(reader.FindSection(id, &data));
    Vector<byte> out;
    out.Resize(data.Size());
    MemCopy(out.Data(), data.Data(), data.Size());
    return out;
}

// Rebuilds `blob` with section `id` replaced (or removed), keeping the dependencies.
Vector<byte> MshWith(const TypeRegistry& types, const Vector<byte>& blob, u32 id, const Vector<byte>& replacement,
                     bool remove = false)
{
    BlobReader reader;
    REQUIRE(reader.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    BlobWriter writer(types, types::kMesh, reader.Id(), 0);
    for (usize d = 0; d < reader.DependencyCount(); ++d)
    {
        writer.AddDependency(reader.DependencyAt(d));
    }
    bool seen = false;
    for (u32 section = kFirstUserSection; section < 48; ++section)
    {
        ByteSpan data;
        if (section == id)
        {
            seen = true;
            if (!remove)
            {
                REQUIRE(writer.AddSection(section, ByteSpan(replacement.Data(), replacement.Size())).Ok());
            }
        }
        else if (reader.FindSection(section, &data))
        {
            REQUIRE(writer.AddSection(section, data).Ok());
        }
    }
    if (!seen && !remove)
    {
        REQUIRE(writer.AddSection(id, ByteSpan(replacement.Data(), replacement.Size())).Ok());
    }
    Vector<byte> out;
    REQUIRE(writer.Finish(&out).Ok());
    return out;
}

Status MshOpen(const TypeRegistry& types, const Vector<byte>& blob)
{
    MeshView view;
    return view.Open(ByteSpan(blob.Data(), blob.Size()), types);
}

f32 MshF32(const byte* p)
{
    f32 v;
    MemCopy(&v, p, 4);
    return v;
}

// A skeleton blob of `n` joints named by `prefix`, with the id kMshSkelId.
Vector<byte> MshSkeletonBlob(const TypeRegistry& types, u32 n, const char* prefix, const char* id = kMshSkelId)
{
    Vector<char> text;
    MshAppend(&text, "id ");
    MshAppend(&text, id);
    MshAppend(&text, "\n");
    for (u32 i = 0; i < n; ++i)
    {
        char line[120];
        char parent[16];
        FormatTo(parent, sizeof(parent), i == 0 ? "-" : "%s%u", prefix, i - 1);
        FormatTo(line, sizeof(line), "joint %s%u %s 0 1 0  0 0 0 1  1 1 1\n", prefix, i, parent);
        MshAppend(&text, line);
    }
    Vector<byte> blob;
    SkeletonSourceError error;
    REQUIRE(MintSkeleton(StringView(text.Data(), text.Size()), types, &blob, &error).Ok());
    return blob;
}

struct MshBad
{
    const char* body;
    u32 line;
};

}  // namespace

TEST_CASE("mesh: a packed grid mints and loads zero-copy", "[asset][mesh]")
{
    TypeRegistry types;
    const Vector<byte> blob = MshMint(types, MshGrid(4));
    MeshView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    CHECK(view.VertexCount() == 25);
    CHECK(view.IndexCount() == 96);
    CHECK(view.IndexBytesPerIndex() == 2);
    CHECK(view.Quality() == MeshQuality::Packed);
    CHECK_FALSE(view.Skinned());
    CHECK(view.VertexStride() == 24);
    CHECK(view.VertexBytes().Size() == 25 * 24);
    CHECK(view.IndexBytes().Size() == 96 * 2);
    CHECK(view.LodCount() == 1);
    CHECK(view.Lod(0).index_count == 96);
    CHECK(view.SlotCount() == 0);
    CHECK(view.BoundsMin()[0] == 0.0f);
    CHECK(view.BoundsMax()[0] == 4.0f);
    CHECK(view.BoundsMax()[2] == 0.0f);
    CHECK(view.CheckIndices().Ok());

    // The attributes: the packed formats at the planned locations.
    REQUIRE(view.AttributeCount() == 4);
    CHECK(view.Attribute(0).format == rhi::VertexFormat::Float3);
    CHECK(view.Attribute(1).format == rhi::VertexFormat::SNorm8x4);
    CHECK(view.Attribute(2).format == rhi::VertexFormat::SNorm8x4);
    CHECK(view.Attribute(3).format == rhi::VertexFormat::UNorm16x2);
    CHECK(view.Attribute(3).offset == 20);

    // Sections are 64-byte aligned inside the blob: the vertex bytes can be uploaded where they are.
    CHECK(static_cast<usize>(view.VertexBytes().Data() - blob.Data()) % kSectionAlignment == 0);
    CHECK(static_cast<usize>(view.IndexBytes().Data() - blob.Data()) % kSectionAlignment == 0);

    // Decoded values: position exact, normal (0,0,1) exact, uv within the recorded error.
    const byte* v = view.VertexBytes().Data() + 7 * 24;
    CHECK(MshF32(v) == 2.0f);
    CHECK(v[14] == 127);
}

TEST_CASE("mesh: the recorded uv error equals an independent measurement", "[asset][mesh]")
{
    TypeRegistry types;
    const Vector<byte> blob = MshMint(types, MshGrid(7, "", 3.7f));
    MeshView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    f64 measured = 0;
    for (u32 y = 0; y <= 7; ++y)
    {
        for (u32 x = 0; x <= 7; ++x)
        {
            const byte* v = view.VertexBytes().Data() + (y * 8 + x) * 24;
            const u32 qu = static_cast<u32>(v[20]) | (static_cast<u32>(v[21]) << 8);
            const u32 qv = static_cast<u32>(v[22]) | (static_cast<u32>(v[23]) << 8);
            const f32 du = static_cast<f32>(view.UvOffset()[0] + view.UvScale()[0] * (qu / 65535.0));
            const f32 dv = static_cast<f32>(view.UvOffset()[1] + view.UvScale()[1] * (qv / 65535.0));
            const f32 su = 3.7f * static_cast<f32>(x) / 7.0f;
            const f32 sv = 3.7f * static_cast<f32>(y) / 7.0f;
            measured = FMax(measured, FMax(static_cast<f64>(Abs(du - su)), static_cast<f64>(Abs(dv - sv))));
        }
    }
    CHECK(Abs(static_cast<f64>(view.UvError()) - measured) < 1e-6);
    CHECK(view.UvError() < 3.7f / 65535.0f);
    CHECK(view.UvScale()[0] > 3.6f);
}

TEST_CASE("mesh: quality full keeps the 48-byte standard layout", "[asset][mesh]")
{
    TypeRegistry types;
    const Vector<byte> blob = MshMint(types, MshGrid(2, "quality full\n"));
    MeshView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    CHECK(view.Quality() == MeshQuality::Full);
    CHECK(view.VertexStride() == 48);
    CHECK(view.Attribute(2).format == rhi::VertexFormat::Float4);
    const byte* v = view.VertexBytes().Data() + 4 * 48;  // vertex (1,1)
    CHECK(MshF32(v) == 1.0f);
    CHECK(MshF32(v + 20) == 1.0f);  // normal z
    CHECK(MshF32(v + 36) == 1.0f);  // tangent w
    CHECK(MshF32(v + 40) == 0.5f);  // uv
    CHECK(view.UvError() == 0.0f);
}

// glTF coverage Day 4: per-vertex colours are a UNorm8x4 attribute at location 4 after the others, sRGB-encoded.
TEST_CASE("mesh: per-vertex colours are a fifth attribute, sRGB-encoded, in packed and full layouts", "[asset][mesh]")
{
    CHECK(EncodeMeshColor(Array<f32, 4>{{0.0f, 1.0f, 0.5f, 0.5f}})[0] == 0);
    CHECK(EncodeMeshColor(Array<f32, 4>{{0.0f, 1.0f, 0.5f, 0.5f}})[1] == 255);
    CHECK(EncodeMeshColor(Array<f32, 4>{{0.0f, 1.0f, 0.5f, 0.5f}})[2] == 188);  // sRGB of linear 0.5
    CHECK(EncodeMeshColor(Array<f32, 4>{{0.0f, 1.0f, 0.5f, 0.5f}})[3] == 128);  // alpha stays linear
    CHECK(EncodeMeshColor(Array<f32, 4>{{-1.0f, 2.0f, 0.0f, 1.0f}})[0] == 0);   // clamped
    CHECK(EncodeMeshColor(Array<f32, 4>{{-1.0f, 2.0f, 0.0f, 1.0f}})[1] == 255);
    TypeRegistry types;
    Array<MeshVertex, 3> vertices;
    vertices[1].position = {{1, 0, 0}};
    vertices[2].position = {{0, 1, 0}};
    const Array<u32, 3> indices = {{0, 1, 2}};
    const Array<Array<u8, 4>, 3> colors = {{{{255, 0, 0, 255}}, {{0, 255, 0, 128}}, {{0, 0, 255, 0}}}};
    for (const MeshQuality quality : {MeshQuality::Packed, MeshQuality::Full})
    {
        MeshBuildInput input;
        input.vertices = Span<const MeshVertex>(vertices.Data(), vertices.Size());
        input.indices = Span<const u32>(indices.Data(), indices.Size());
        input.colors = Span<const Array<u8, 4>>(colors.Data(), colors.Size());
        input.quality = quality;
        Vector<byte> blob;
        REQUIRE(BuildMeshBlob(types, ResourceId::FromValue(0x51), input, &blob).Ok());
        MeshView view;
        REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
        const u32 base = quality == MeshQuality::Packed ? 24 : 48;
        CHECK(view.HasColors());
        CHECK(view.VertexStride() == base + 4);
        REQUIRE(view.AttributeCount() == 5);
        CHECK(view.Attribute(4).location == kMeshLocationColor);
        CHECK(view.Attribute(4).format == rhi::VertexFormat::UNorm8x4);
        CHECK(view.Attribute(4).offset == base);
        const byte* second = view.VertexBytes().Data() + (base + 4);
        CHECK(second[base + 1] == 255);
        CHECK(second[base + 3] == 128);
        input.colors = Span<const Array<u8, 4>>(colors.Data(), 2);  // not one per vertex
        CHECK(BuildMeshBlob(types, ResourceId::FromValue(0x51), input, &blob).Failed());
    }
    // Without colours nothing changes: the attribute is absent.
    const Vector<byte> plain = MshMint(types, MshGrid(1));
    MeshView view;
    REQUIRE(view.Open(ByteSpan(plain.Data(), plain.Size()), types).Ok());
    CHECK_FALSE(view.HasColors());
}

// glTF coverage Day 6: morph targets are sections 40 and 41; zero deltas are dropped, the box holds every target at
// weights from -1 to 1, and a delta past the vertices or out of order is refused.
TEST_CASE("mesh: morph targets round-trip, grow the box and refuse bad deltas", "[asset][mesh]")
{
    TypeRegistry types;
    Array<MeshVertex, 3> vertices;
    vertices[1].position = {{1, 0, 0}};
    vertices[2].position = {{0, 1, 0}};
    const Array<u32, 3> indices = {{0, 1, 2}};
    Array<MeshMorphDelta, 3> smile;
    smile[0].vertex = 0;  // all zero: dropped
    smile[1].vertex = 1;
    smile[1].position = {{0, 0, 2}};
    smile[2].vertex = 2;
    smile[2].normal = {{0.5f, 0, 0}};
    Array<MeshMorphDelta, 1> blink;
    blink[0].vertex = 2;
    blink[0].position = {{0, -3, 0}};
    Array<MeshMorphTarget, 2> targets;
    MemCopy(targets[0].name.Data(), "smile", 5);
    targets[0].deltas = Span<const MeshMorphDelta>(smile.Data(), smile.Size());
    MemCopy(targets[1].name.Data(), "blink", 5);
    targets[1].deltas = Span<const MeshMorphDelta>(blink.Data(), blink.Size());
    MeshBuildInput input;
    input.vertices = Span<const MeshVertex>(vertices.Data(), vertices.Size());
    input.indices = Span<const u32>(indices.Data(), indices.Size());
    input.morph_targets = Span<const MeshMorphTarget>(targets.Data(), targets.Size());
    Vector<byte> blob;
    CHECK(BuildMeshBlob(types, ResourceId::FromValue(0x61), input, &blob).Failed());  // packed: refused
    input.quality = MeshQuality::Full;
    REQUIRE(BuildMeshBlob(types, ResourceId::FromValue(0x61), input, &blob).Ok());
    MeshView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    REQUIRE(view.MorphTargetCount() == 2);
    CHECK(view.MorphDeltaCount() == 3);
    CHECK(StringView(view.MorphTarget(0).name.Data()) == StringView("smile"));
    CHECK(view.MorphTarget(0).delta_count == 2);
    CHECK(view.MorphTarget(1).first_delta == 2);
    CHECK(view.MorphDelta(0).vertex == 1);
    CHECK(view.MorphDelta(0).position[2] == 2.0f);
    CHECK(view.MorphDelta(2).position[1] == -3.0f);
    CHECK(view.BoundsMax()[2] == 2.0f);   // smile at weight 1
    CHECK(view.BoundsMin()[2] == -2.0f);  // and at -1
    CHECK(view.BoundsMin()[1] == -3.0f);  // blink
    smile[2].vertex = 1;                  // out of order
    CHECK(BuildMeshBlob(types, ResourceId::FromValue(0x61), input, &blob).Failed());
    smile[2].vertex = 3;  // past the vertices
    CHECK(BuildMeshBlob(types, ResourceId::FromValue(0x61), input, &blob).Failed());
}

TEST_CASE("mesh: minting twice is byte-identical", "[asset][mesh]")
{
    TypeRegistry types;
    const Vector<byte> a = MshMint(types, MshGrid(5));
    const Vector<byte> b = MshMint(types, MshGrid(5));
    REQUIRE(a.Size() == b.Size());
    CHECK(MemCompare(a.Data(), b.Data(), a.Size()) == 0);
}

TEST_CASE("mesh: more than 65536 vertices use 32-bit indices", "[asset][mesh]")
{
    TypeRegistry types;
    const Vector<byte> blob = MshMint(types, MshGrid(256));  // 257 * 257 = 66049 vertices
    MeshView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    CHECK(view.VertexCount() == 66049);
    CHECK(view.IndexBytesPerIndex() == 4);
    CHECK(view.CheckIndices().Ok());
}

#define MSH_ID "id 00000000000000bb\n"
#define MSH_V0 "v 0 0 0  0 0 1  1 0 0 1  0 0\n"
#define MSH_V1 "v 1 0 0  0 0 1  1 0 0 1  1 0\n"
#define MSH_V2 "v 0 1 0  0 0 1  1 0 0 1  0 1\n"
#define MSH_TRI MSH_V0 MSH_V1 MSH_V2 "i 0 1 2\n"

TEST_CASE("mesh source: each error names its line", "[asset][mesh]")
{
    TypeRegistry types;
    const MshBad cases[] = {
        {MSH_V0, 1},                                                           // no id first
        {MSH_ID MSH_ID, 2},                                                    // id twice
        {"id zz\n", 1},                                                        // bad id
        {MSH_ID "thing\n", 2},                                                 // unknown record
        {MSH_ID "quality tiny\n", 2},                                          // bad quality
        {MSH_ID MSH_V0 "quality full\n", 3},                                   // quality after vertices
        {MSH_ID "v 0 0 0 0 0 1 1 0 0 1 0\n", 2},                               // too few numbers
        {MSH_ID "v 0 0 0 0 0 1 1 0 0 1 0 nan\n", 2},                           // NaN
        {MSH_ID "v 0 0 0 0 0 0 1 0 0 1 0 0\n", 2},                             // zero normal
        {MSH_ID "v 0 0 0 0 0 1 1 0 0 2 0 0\n", 2},                             // bitangent sign not +-1
        {MSH_ID MSH_V0 "i 0 0 1\n", 3},                                        // index past the vertices defined
        {MSH_ID MSH_V0 "i 0 0\n", 3},                                          // too few indices
        {MSH_ID MSH_V0 "i 0 0 x\n", 3},                                        // not a number
        {MSH_TRI MSH_ID, 1},                                                   // id after content (twice or late)
        {MSH_ID MSH_TRI "v 0 0 0  0 0 1  1 0 0 1  0 0\n", 6},                  // vertex after an index
        {MSH_ID MSH_TRI "lod 0 3\n", 6},                                       // lod needs three fields
        {MSH_ID MSH_TRI "slot toolongname_toolongname_toolongname 0 3\n", 6},  // 32-byte name
        {MSH_ID MSH_TRI "skin 0 0 1\n", 6},                                    // skin without skeleton
        {MSH_ID "skeleton 00000000000000aa\nskeleton 00000000000000aa\n", 3},  // skeleton twice
        {MSH_ID "skeleton 00000000000000aa\n" MSH_TRI "skin 9 0 1\n", 7},      // skin for an unknown vertex
        {MSH_ID "skeleton 00000000000000aa\n" MSH_TRI "skin 0 300 1\n", 7},    // joint above 255
        {MSH_ID "skeleton 00000000000000aa\n" MSH_TRI "skin 0 1 -1\n", 7},     // negative weight
        {MSH_ID "skeleton 00000000000000aa\n" MSH_TRI "skin 0 1 1\nskin 0 1 1\n", 8},  // skinned twice
        {MSH_ID "skeleton 00000000000000aa\n" MSH_TRI "skin 0 1 1\nskin 1 1 1\n",
         5},  // vertex 2 has no skin: its v line
        {MSH_ID "skeleton 00000000000000aa\n" MSH_TRI "skin 0 1 0 2 0\nskin 1 1 1\nskin 2 1 1\n",
         3},                               // no weight above 0
        {MSH_ID, 0},                       // nothing
        {MSH_ID MSH_V0 MSH_V1 MSH_V2, 0},  // no indices
    };
    for (const MshBad& c : cases)
    {
        INFO(c.body);
        Vector<byte> blob;
        MeshSourceError error;
        const Status s = MintMesh(StringView(c.body, CStrLength(c.body)), types, nullptr, &blob, &error);
        CHECK(s.Failed());
        CHECK(error.line == c.line);
        CHECK(CStrLength(error.message.Data()) > 0);
    }
    // A missing id names `asset_mint adopt`.
    Vector<byte> blob;
    MeshSourceError error;
    CHECK(MintMesh(StringView(MSH_V0, CStrLength(MSH_V0)), types, nullptr, &blob, &error).Failed());
    CHECK(StringView(error.message.Data()).Contains(StringView("adopt")));
}

TEST_CASE("mesh source: lods and slots are checked against the indices", "[asset][mesh]")
{
    TypeRegistry types;
    const char* bad[] = {
        MSH_ID MSH_TRI "lod 0 6 0.5\n",               // past the indices
        MSH_ID MSH_TRI "lod 0 2 0.5\n",               // not whole triangles
        MSH_ID MSH_TRI "lod 0 3 0.5\nlod 0 3 0.7\n",  // screen sizes grow
        MSH_ID MSH_TRI "slot a 0 3\nslot a 0 3\n",    // duplicate slot
        MSH_ID MSH_TRI "slot a 3 3\n",                // past the indices
    };
    for (const char* text : bad)
    {
        INFO(text);
        Vector<byte> blob;
        MeshSourceError error;
        CHECK(MintMesh(StringView(text, CStrLength(text)), types, nullptr, &blob, &error).Failed());
    }
    // Two LODs and two slots on a 2-triangle mesh.
    const char* good = MSH_ID MSH_V0 MSH_V1 MSH_V2 "v 1 1 0  0 0 1  1 0 0 1  1 1\ni 0 1 2\ni 1 3 2\n"
                                                   "lod 0 6 0.5\nlod 3 3 0.1\nslot skin 0 3\nslot cloth 3 3\n";
    Vector<byte> blob;
    MeshSourceError error;
    REQUIRE(MintMesh(StringView(good, CStrLength(good)), types, nullptr, &blob, &error).Ok());
    MeshView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    CHECK(view.LodCount() == 2);
    CHECK(view.Lod(1).first_index == 3);
    CHECK(view.Lod(1).screen_size == 0.1f);
    REQUIRE(view.SlotCount() == 2);
    CHECK(StringView(view.Slot(1).name.Data()) == StringView("cloth"));
    CHECK(view.Slot(1).first_index == 3);
}

TEST_CASE("mesh: skin weights sum to 255 and follow the four-largest rule", "[asset][mesh]")
{
    MeshSkinInfluence q;
    {
        const u32 joints[5] = {3, 1, 7, 2, 9};
        const f64 weights[5] = {0.1, 0.4, 0.2, 0.2, 0.05};
        REQUIRE(QuantizeSkinInfluences(joints, weights, 5, &q));
        // The four largest: 1 (0.4), then 2 and 7 (0.2 each; the tie goes to the lower joint), then 3 (0.1).
        CHECK(q.joint[0] == 1);
        CHECK(q.joint[1] == 2);
        CHECK(q.joint[2] == 7);
        CHECK(q.joint[3] == 3);
        CHECK(static_cast<u32>(q.weight[0]) + q.weight[1] + q.weight[2] + q.weight[3] == 255);
        CHECK(q.weight[0] > q.weight[1]);
        CHECK(q.weight[1] == q.weight[2]);
    }
    {
        const u32 joints[3] = {5, 6, 4};
        const f64 weights[3] = {1.0, 1.0, 1.0};  // 85 each, ties by joint index: 4, 5, 6
        REQUIRE(QuantizeSkinInfluences(joints, weights, 3, &q));
        CHECK(q.joint[0] == 4);
        CHECK(q.weight[0] == 85);
        CHECK(q.weight[3] == 0);
    }
    {
        const u32 joints[1] = {200};
        const f64 weights[1] = {0.001};
        REQUIRE(QuantizeSkinInfluences(joints, weights, 1, &q));
        CHECK(q.weight[0] == 255);
    }
    {
        const u32 joints[2] = {0, 1};
        const f64 weights[2] = {0.0, 0.0};
        CHECK_FALSE(QuantizeSkinInfluences(joints, weights, 2, &q));
    }
    // Property: random weights always sum to exactly 255.
    test::Rng rng(77);
    for (u32 round = 0; round < 2000; ++round)
    {
        u32 joints[6];
        f64 weights[6];
        const u32 n = 1 + rng.Below(6);
        for (u32 k = 0; k < n; ++k)
        {
            joints[k] = rng.Below(256);
            weights[k] = static_cast<f64>(rng.Below(100000)) / 997.0 + (k == 0 ? 0.001 : 0.0);
        }
        REQUIRE(QuantizeSkinInfluences(joints, weights, n, &q));
        u32 sum = 0;
        for (u32 k = 0; k < 4; ++k)
        {
            sum += q.weight[k];
            if (k > 0)
            {
                CHECK((q.weight[k - 1] > q.weight[k] ||
                       (q.weight[k - 1] == q.weight[k] && (q.weight[k] == 0 || q.joint[k - 1] <= q.joint[k]))));
            }
        }
        CHECK(sum == 255);
    }
}

TEST_CASE("mesh: a skinned mesh records its skeleton and refuses another", "[asset][mesh]")
{
    TypeRegistry types;
    const Vector<byte> skeleton_blob = MshSkeletonBlob(types, 3, "j");
    SkeletonView skeleton;
    REQUIRE(skeleton.Open(ByteSpan(skeleton_blob.Data(), skeleton_blob.Size()), types).Ok());

    const char* source = MSH_ID "skeleton 00000000000000aa\n" MSH_TRI
                                "skin 0 0 0.5 1 0.5\nskin 1 2 1\nskin 2 1 0.3 0 0.3 2 0.4\n";
    Vector<byte> blob;
    MeshSourceError error;
    REQUIRE(MintMesh(StringView(source, CStrLength(source)), types, &skeleton, &blob, &error).Ok());
    MeshView view;
    REQUIRE(view.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    CHECK(view.Skinned());
    CHECK(view.Skeleton() == skeleton.Id());
    CHECK(view.SkeletonHash() == skeleton.Hash());
    CHECK(view.SkinJointCount() == 3);
    REQUIRE(view.AttributeCount() == 6);
    CHECK(view.Attribute(4).format == rhi::VertexFormat::UInt8x4);
    CHECK(view.Attribute(4).location == kMeshLocationJoints);
    CHECK(view.Attribute(4).binding == 1);
    CHECK(view.Attribute(5).format == rhi::VertexFormat::UNorm8x4);
    CHECK(view.SkinBytes().Size() == 3 * 8);
    for (u32 v = 0; v < 3; ++v)
    {
        const byte* p = view.SkinBytes().Data() + v * 8;
        CHECK(static_cast<u32>(p[4]) + p[5] + p[6] + p[7] == 255);
    }
    // Vertex 0: 50/50 between joints 0 and 1 (255 splits 128 + 127; the remainder goes to the lower joint).
    const byte* p0 = view.SkinBytes().Data();
    CHECK(p0[0] == 0);
    CHECK(p0[1] == 1);
    CHECK(static_cast<u32>(p0[4]) + p0[5] == 255);
    // Vertex 2: joints 2 (0.4) first, then 0 and 1 (0.3 each, the lower joint first).
    const byte* p2 = view.SkinBytes().Data() + 16;
    CHECK(p2[0] == 2);
    CHECK(p2[1] == 0);
    CHECK(p2[2] == 1);
    // The joint boxes: joint 2 influences only vertex 1 at (1, 0, 0).
    CHECK(view.JointBounds(2).used);
    CHECK(view.JointBounds(2).min[0] == 0.0f);  // vertices 1 and 2 (0,1,0) and (1,0,0)
    CHECK(view.JointBounds(2).max[0] == 1.0f);
    CHECK(view.CheckSkeleton(skeleton).Ok());
    CHECK(view.CheckSkin().Ok());
    {
        // Attributes that shrink the skin stride below the 8 bytes CheckSkin reads, and an offset that would overflow.
        Vector<byte> attributes = MshSection(types, blob, kMeshSectionAttributes);
        Vector<byte> bad = MshCopy(attributes);
        bad[5 * 16 + 8] = 0;  // the weights at offset 0: stride 4
        CHECK(MshOpen(types, MshWith(types, blob, kMeshSectionAttributes, bad)).Failed());
        bad = MshCopy(attributes);
        bad[5 * 16 + 11] = 0xff;  // offset near 2^32
        bad[5 * 16 + 10] = 0xff;
        CHECK(MshOpen(types, MshWith(types, blob, kMeshSectionAttributes, bad)).Failed());
    }
    {
        // A hostile skin stream: weights that do not sum to 255, and a joint past the count.
        Vector<byte> stream = MshSection(types, blob, kMeshSectionSkin);
        stream[4] = 1;
        const Vector<byte> bad_sum = MshWith(types, blob, kMeshSectionSkin, stream);
        MeshView bad;
        REQUIRE(bad.Open(ByteSpan(bad_sum.Data(), bad_sum.Size()), types).Ok());
        CHECK(bad.CheckSkin().Code() == ErrorCode::Corrupt);
        stream = MshSection(types, blob, kMeshSectionSkin);
        stream[8] = 9;  // vertex 1's strongest joint
        const Vector<byte> bad_joint = MshWith(types, blob, kMeshSectionSkin, stream);
        REQUIRE(bad.Open(ByteSpan(bad_joint.Data(), bad_joint.Size()), types).Ok());
        CHECK(bad.CheckSkin().Code() == ErrorCode::Corrupt);
    }

    // The same skeleton id with other joint names: the hash differs, so the mesh is refused.
    const Vector<byte> other_blob = MshSkeletonBlob(types, 3, "k");
    SkeletonView other;
    REQUIRE(other.Open(ByteSpan(other_blob.Data(), other_blob.Size()), types).Ok());
    CHECK(view.CheckSkeleton(other).Code() == ErrorCode::DependencyTypeMismatch);
    // Another id with the same joints: refused too.
    const Vector<byte> third_blob = MshSkeletonBlob(types, 3, "j", "00000000000000ac");
    SkeletonView third;
    REQUIRE(third.Open(ByteSpan(third_blob.Data(), third_blob.Size()), types).Ok());
    CHECK(view.CheckSkeleton(third).Code() == ErrorCode::DependencyTypeMismatch);
    // A different joint count.
    const Vector<byte> bigger_blob = MshSkeletonBlob(types, 4, "j");
    SkeletonView bigger;
    REQUIRE(bigger.Open(ByteSpan(bigger_blob.Data(), bigger_blob.Size()), types).Ok());
    CHECK(view.CheckSkeleton(bigger).Code() == ErrorCode::DependencyTypeMismatch);

    // The blob declares the skeleton as a dependency with the skin role.
    BlobReader reader;
    REQUIRE(reader.Open(ByteSpan(blob.Data(), blob.Size()), types).Ok());
    REQUIRE(reader.DependencyCount() == 1);
    CHECK(reader.DependencyAt(0).role == kMeshRoleSkinSkeleton);
    CHECK(reader.DependencyAt(0).type == types::kSkeleton);

    // A joint past the skeleton's count is refused with the line of the skin record.
    const char* bad = MSH_ID "skeleton 00000000000000aa\n" MSH_TRI "skin 0 0 1\nskin 1 3 1\nskin 2 0 1\n";
    CHECK(MintMesh(StringView(bad, CStrLength(bad)), types, &skeleton, &blob, &error).Failed());
    CHECK(error.line == 8);
    // The wrong skeleton blob, or none, is refused at the skeleton line.
    CHECK(MintMesh(StringView(source, CStrLength(source)), types, nullptr, &blob, &error).Failed());
    CHECK(error.line == 2);
    CHECK(MintMesh(StringView(source, CStrLength(source)), types, &third, &blob, &error).Failed());
    CHECK(error.line == 2);
}

TEST_CASE("mesh: hostile blobs are refused", "[asset][mesh]")
{
    TypeRegistry types;
    const Vector<byte> good = MshMint(types, MshGrid(3, "", 1.0f));
    REQUIRE(MshOpen(types, good).Ok());

    SECTION("counts that do not match the sections")
    {
        Vector<byte> header = MshSection(types, good, kMeshSectionHeader);
        header[0] = 17;  // vertex count
        CHECK(MshOpen(types, MshWith(types, good, kMeshSectionHeader, header)).Failed());
        header = MshSection(types, good, kMeshSectionHeader);
        header[4] = 95;  // index count not a multiple of 3
        CHECK(MshOpen(types, MshWith(types, good, kMeshSectionHeader, header)).Failed());
        header = MshSection(types, good, kMeshSectionHeader);
        for (u32 k = 0; k < 4; ++k)
        {
            header[k] = 0xff;  // vertex count 2^32 - 1
        }
        CHECK(MshOpen(types, MshWith(types, good, kMeshSectionHeader, header)).Failed());
        header = MshSection(types, good, kMeshSectionHeader);
        header[8] = 3;  // index width
        CHECK(MshOpen(types, MshWith(types, good, kMeshSectionHeader, header)).Failed());
        header = MshSection(types, good, kMeshSectionHeader);
        header[13] = 1;  // an undefined flag
        CHECK(MshOpen(types, MshWith(types, good, kMeshSectionHeader, header)).Failed());
    }
    SECTION("a stream of the wrong size, or a missing section")
    {
        Vector<byte> vertices = MshSection(types, good, kMeshSectionVertices);
        vertices.Resize(vertices.Size() - 4);
        CHECK(MshOpen(types, MshWith(types, good, kMeshSectionVertices, vertices)).Failed());
        Vector<byte> indices = MshSection(types, good, kMeshSectionIndices);
        indices.Resize(indices.Size() - 2);
        CHECK(MshOpen(types, MshWith(types, good, kMeshSectionIndices, indices)).Failed());
        CHECK(MshOpen(types, MshWith(types, good, kMeshSectionLods, Vector<byte>(), true)).Failed());
        CHECK(MshOpen(types, MshWith(types, good, kMeshSectionAttributes, Vector<byte>(), true)).Failed());
    }
    SECTION("an attribute that is out of range, misaligned or repeats a location")
    {
        Vector<byte> attributes = MshSection(types, good, kMeshSectionAttributes);
        Vector<byte> bad = MshCopy(attributes);
        bad[4] = 200;  // format
        CHECK(MshOpen(types, MshWith(types, good, kMeshSectionAttributes, bad)).Failed());
        bad = MshCopy(attributes);
        bad[8 + 16] = 13;  // offset not a multiple of 4
        CHECK(MshOpen(types, MshWith(types, good, kMeshSectionAttributes, bad)).Failed());
        bad = MshCopy(attributes);
        bad[16] = 0;  // location 0 twice
        CHECK(MshOpen(types, MshWith(types, good, kMeshSectionAttributes, bad)).Failed());
        bad = MshCopy(attributes);
        bad[12] = 1;  // binding 1 without a skin
        CHECK(MshOpen(types, MshWith(types, good, kMeshSectionAttributes, bad)).Failed());
    }
    SECTION("a LOD or slot range past the indices, or LOD sizes that grow")
    {
        Vector<byte> lods = MshSection(types, good, kMeshSectionLods);
        lods[4] = 0xff;
        CHECK(MshOpen(types, MshWith(types, good, kMeshSectionLods, lods)).Failed());
    }
    SECTION("a non-finite or inverted bound")
    {
        Vector<byte> header = MshSection(types, good, kMeshSectionHeader);
        const f32 nan = Limits<f32>::QuietNaN();
        MemCopy(header.Data() + 32, &nan, 4);
        CHECK(MshOpen(types, MshWith(types, good, kMeshSectionHeader, header)).Failed());
        header = MshSection(types, good, kMeshSectionHeader);
        const f32 big = 100.0f;
        MemCopy(header.Data() + 32, &big, 4);  // min x above max x
        CHECK(MshOpen(types, MshWith(types, good, kMeshSectionHeader, header)).Failed());
    }
    SECTION("a morph target section without its deltas, another type, truncation and an old version")
    {
        Vector<byte> morph;
        morph.Resize(48);
        CHECK(MshOpen(types, MshWith(types, good, kMeshSectionMorphTargets, morph)).Code() == ErrorCode::Corrupt);

        BlobWriter other(types, types::kNames, ResourceId::FromValue(5), 0);
        Vector<byte> not_a_mesh;
        REQUIRE(other.Finish(&not_a_mesh).Ok());
        CHECK(MshOpen(types, not_a_mesh).Failed());

        for (usize cut = 0; cut < good.Size(); cut += 11)
        {
            MeshView view;
            CHECK(view.Open(ByteSpan(good.Data(), cut), types).Failed());
        }
        Vector<byte> v0 = MshCopy(good);
        v0[12] = 0;
        const Status s = MshOpen(types, v0);
        CHECK(s.Code() == ErrorCode::UnsupportedVersion);
        CHECK(StringView(s.Message()).Contains(StringView("asset_mint remint")));
    }
    SECTION("an index past the vertices is found by CheckIndices")
    {
        Vector<byte> indices = MshSection(types, good, kMeshSectionIndices);
        indices[0] = 200;
        const Vector<byte> bad = MshWith(types, good, kMeshSectionIndices, indices);
        MeshView view;
        REQUIRE(view.Open(ByteSpan(bad.Data(), bad.Size()), types).Ok());
        CHECK(view.CheckIndices().Code() == ErrorCode::Corrupt);
    }
}

TEST_CASE("mesh: a skinned blob without its dependency, or a static one with a skin section, is refused",
          "[asset][mesh]")
{
    TypeRegistry types;
    const Vector<byte> skeleton_blob = MshSkeletonBlob(types, 2, "j");
    SkeletonView skeleton;
    REQUIRE(skeleton.Open(ByteSpan(skeleton_blob.Data(), skeleton_blob.Size()), types).Ok());
    const char* source = MSH_ID "skeleton 00000000000000aa\n" MSH_TRI "skin 0 0 1\nskin 1 1 1\nskin 2 0 1\n";
    Vector<byte> skinned;
    MeshSourceError error;
    REQUIRE(MintMesh(StringView(source, CStrLength(source)), types, &skeleton, &skinned, &error).Ok());

    // Rebuild the skinned blob without its dependency.
    BlobReader reader;
    REQUIRE(reader.Open(ByteSpan(skinned.Data(), skinned.Size()), types).Ok());
    BlobWriter writer(types, types::kMesh, reader.Id(), 0);
    for (u32 section = kFirstUserSection; section < 48; ++section)
    {
        ByteSpan data;
        if (reader.FindSection(section, &data))
        {
            REQUIRE(writer.AddSection(section, data).Ok());
        }
    }
    Vector<byte> no_dependency;
    REQUIRE(writer.Finish(&no_dependency).Ok());
    CHECK(MshOpen(types, no_dependency).Failed());

    // A static mesh with a skin section added.
    const Vector<byte> plain = MshMint(types, MshGrid(2));
    Vector<byte> skin;
    skin.Resize(9 * 8);
    CHECK(MshOpen(types, MshWith(types, plain, kMeshSectionSkin, skin)).Failed());
}

TEST_CASE("mesh: BuildMeshBlob refuses what the minter would never produce", "[asset][mesh]")
{
    TypeRegistry types;
    MeshVertex vertices[3];
    vertices[1].position = {{1, 0, 0}};
    vertices[2].position = {{0, 1, 0}};
    const u32 indices[3] = {0, 1, 2};
    MeshBuildInput input;
    input.vertices = Span<const MeshVertex>(vertices, 3);
    input.indices = Span<const u32>(indices, 3);
    Vector<byte> out;
    const ResourceId id = ResourceId::FromValue(9);
    CHECK(BuildMeshBlob(types, id, input, &out).Ok());
    vertices[0].normal = {{0, 0, 0}};
    CHECK(BuildMeshBlob(types, id, input, &out).Failed());
    vertices[0].normal = {{0, 0, 1}};
    vertices[1].position = {{Limits<f32>::Infinity(), 0, 0}};
    CHECK(BuildMeshBlob(types, id, input, &out).Failed());
    vertices[1].position = {{1, 0, 0}};
    const u32 bad_indices[3] = {0, 1, 3};
    input.indices = Span<const u32>(bad_indices, 3);
    CHECK(BuildMeshBlob(types, id, input, &out).Failed());
    input.indices = Span<const u32>(indices, 3);
    // A skin whose weights do not sum to 255.
    MeshSkinInfluence skin[3];
    for (MeshSkinInfluence& s : skin)
    {
        s.weight[0] = 200;
    }
    input.skin = Span<const MeshSkinInfluence>(skin, 3);
    input.skeleton = ResourceId::FromValue(0xaa);
    input.skeleton_joint_count = 2;
    CHECK(BuildMeshBlob(types, id, input, &out).Failed());
    for (MeshSkinInfluence& s : skin)
    {
        s.weight[0] = 255;
    }
    CHECK(BuildMeshBlob(types, id, input, &out).Ok());
    skin[1].joint[0] = 2;  // at the joint count
    CHECK(BuildMeshBlob(types, id, input, &out).Failed());
    skin[1].joint[0] = 1;
    skin[1].joint[1] = 1;  // the same joint twice
    skin[1].weight[0] = 100;
    skin[1].weight[1] = 155;
    CHECK(BuildMeshBlob(types, id, input, &out).Failed());
}

}  // namespace engine_placeholder::asset
