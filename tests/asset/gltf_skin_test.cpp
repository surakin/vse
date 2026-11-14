// Phase 22 Day 337: importing glTF skins as skeletons, meshes skinned to them, and animations as clips
// (asset_import/gltf_skin.h, gltf_clip.h, gltf_nodes.h).

#include <catch2/catch_test_macros.hpp>

#include "asset/clip.h"
#include "asset/mesh.h"
#include "asset/skeleton.h"
#include "asset_import/gltf_clip.h"
#include "asset_import/gltf_nodes.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "gltf_writer.h"
#include "lang/scalar.h"
#include "os/os.h"
#include "support.h"

namespace engine_placeholder::asset
{
namespace
{

using test::GltfWriter;

void Put(String<HeapAllocator>* out, const char* text)
{
    out->Append(StringView(text, CStrLength(text)));
}

template <typename... Args> void Fmt(String<HeapAllocator>* out, const char* format, Args... args)
{
    char buffer[1024];
    FormatTo(buffer, sizeof(buffer), format, args...);
    Put(out, buffer);
}

template <typename T>
u32 SkAcc(GltfWriter* w, const T* values, usize count, u32 component_type, const char* type, const char* extra = "")
{
    const StringView name(type);
    const u32 components = name == StringView("SCALAR") ? 1
                           : name == StringView("MAT4") ? 16
                                                        : static_cast<u32>(type[3] - '0');
    return w->AddAccessor(w->AddView(values, count * sizeof(T)), component_type, static_cast<u32>(count) / components,
                          type, extra);
}

// A node of a test file: its transform, its parent (-1 for a root) and whether it carries a mesh or a skin.
struct TestNode
{
    const char* name;
    i32 parent;
    f64 t[3];
    f64 q[4];
    f64 s[3];
    i32 mesh = -1;
    i32 skin = -1;
};

String<HeapAllocator> NodesJson(const TestNode* nodes, usize count)
{
    String<HeapAllocator> out(HeapAllocator::Default(), StringView("\"nodes\":["));
    for (usize i = 0; i < count; ++i)
    {
        const TestNode& n = nodes[i];
        Put(&out, i == 0 ? "{" : ",{");
        if (n.name != nullptr)
        {
            Fmt(&out, "\"name\":\"%s\",", n.name);
        }
        Fmt(&out, "\"translation\":[%.9g,%.9g,%.9g],\"rotation\":[%.9g,%.9g,%.9g,%.9g],\"scale\":[%.9g,%.9g,%.9g]",
            n.t[0], n.t[1], n.t[2], n.q[0], n.q[1], n.q[2], n.q[3], n.s[0], n.s[1], n.s[2]);
        if (n.mesh >= 0)
        {
            Fmt(&out, ",\"mesh\":%d", static_cast<int>(n.mesh));
        }
        if (n.skin >= 0)
        {
            Fmt(&out, ",\"skin\":%d", static_cast<int>(n.skin));
        }
        bool any = false;
        for (usize c = 0; c < count; ++c)
        {
            if (nodes[c].parent == static_cast<i32>(i))
            {
                Fmt(&out, "%s%u", any ? "," : ",\"children\":[", static_cast<unsigned>(c));
                any = true;
            }
        }
        Put(&out, any ? "]}" : "}");
    }
    Put(&out, "]");
    return out;
}

// A reference matrix algebra, written apart from the importer's: column-major 4x4 in doubles.
struct Mat
{
    f64 a[16];
};

Mat Identity()
{
    Mat m = {};
    m.a[0] = m.a[5] = m.a[10] = m.a[15] = 1;
    return m;
}

Mat Mul(const Mat& x, const Mat& y)
{
    Mat r = {};
    for (int c = 0; c < 4; ++c)
    {
        for (int row = 0; row < 4; ++row)
        {
            for (int k = 0; k < 4; ++k)
            {
                r.a[c * 4 + row] += x.a[k * 4 + row] * y.a[c * 4 + k];
            }
        }
    }
    return r;
}

Mat FromTrs(const f64* t, const f64* q, const f64* s)
{
    const f64 x = q[0];
    const f64 y = q[1];
    const f64 z = q[2];
    const f64 w = q[3];
    Mat m = Identity();
    m.a[0] = (1 - 2 * (y * y + z * z)) * s[0];
    m.a[1] = 2 * (x * y + z * w) * s[0];
    m.a[2] = 2 * (x * z - y * w) * s[0];
    m.a[4] = 2 * (x * y - z * w) * s[1];
    m.a[5] = (1 - 2 * (x * x + z * z)) * s[1];
    m.a[6] = 2 * (y * z + x * w) * s[1];
    m.a[8] = 2 * (x * z + y * w) * s[2];
    m.a[9] = 2 * (y * z - x * w) * s[2];
    m.a[10] = (1 - 2 * (x * x + y * y)) * s[2];
    m.a[12] = t[0];
    m.a[13] = t[1];
    m.a[14] = t[2];
    return m;
}

Mat WorldOf(const TestNode* nodes, i32 index)
{
    Mat local = FromTrs(nodes[index].t, nodes[index].q, nodes[index].s);
    return nodes[index].parent < 0 ? local : Mul(WorldOf(nodes, nodes[index].parent), local);
}

// The model-space bind matrix of every joint of a skeleton blob.
Vector<Mat> SkeletonWorlds(const SkeletonView& view)
{
    Vector<Mat> worlds;
    for (u32 j = 0; j < view.JointCount(); ++j)
    {
        const Array<f32, 3> t = view.Translation(j);
        const Array<f32, 4> q = view.Rotation(j);
        const Array<f32, 3> s = view.Scale(j);
        const f64 td[3] = {t[0], t[1], t[2]};
        const f64 qd[4] = {q[0], q[1], q[2], q[3]};
        const f64 sd[3] = {s[0], s[1], s[2]};
        Mat local = FromTrs(td, qd, sd);
        const u16 parent = view.Parent(j);
        worlds.Push(parent == kNoParent ? local : Mul(worlds[parent], local));
    }
    return worlds;
}

f64 Worse(f64 a, f64 b)
{
    return a > b ? a : b;
}

f64 MaxDifference(const Mat& x, const Mat& y)
{
    f64 worst = 0;
    for (int i = 0; i < 16; ++i)
    {
        const f64 d = Abs(x.a[i] - y.a[i]);
        worst = d > worst ? d : worst;
    }
    return worst;
}

Status SkImport(const Vector<byte>& glb, ImportResult* result, GltfError* error,
                ImportOptions options = ImportOptions())
{
    NoGltfSource none;
    SeededIdSource ids(11);
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

bool SkSays(const GltfError& error, const char* text)
{
    return StringView(error.message.Data(), CStrLength(error.message.Data())).Contains(StringView(text));
}

bool NoteSays(const ImportResult& result, const char* text)
{
    for (usize i = 0; i < result.report.notes.Size(); ++i)
    {
        if (result.report.notes[i].View().Contains(StringView(text)))
        {
            return true;
        }
    }
    return false;
}

const ImportedResource* Find(const ImportResult& result, ResourceType type, usize nth = 0)
{
    for (usize i = 0; i < result.resources.Size(); ++i)
    {
        if (result.resources[i].type == type && nth-- == 0)
        {
            return &result.resources[i];
        }
    }
    return nullptr;
}

ByteSpan BlobOf(const ImportedResource& r)
{
    return ByteSpan(r.blob.Data(), r.blob.Size());
}

bool SkSame(const Vector<byte>& a, const Vector<byte>& b)
{
    return a.Size() == b.Size() && MemCompare(a.Data(), b.Data(), a.Size()) == 0;
}

// A glTF of `nodes` with one skin over `joint_nodes`, and the rest of the root members `more`.
Vector<byte> SkinGlb(GltfWriter* w, const TestNode* nodes, usize node_count, const u32* joint_nodes, usize joint_count,
                     const char* skin_extra = "", const char* more = "")
{
    if (w->bin.Size() == 0)
    {
        const u32 padding = 0;
        w->AddView(&padding, sizeof(padding));  // a buffer needs some bytes
    }
    String<HeapAllocator> json = NodesJson(nodes, node_count);
    Put(&json, ",\"skins\":[{\"joints\":[");
    for (usize i = 0; i < joint_count; ++i)
    {
        Fmt(&json, "%s%u", i == 0 ? "" : ",", static_cast<unsigned>(joint_nodes[i]));
    }
    Put(&json, "]");
    Put(&json, skin_extra);
    Put(&json, "}]");
    if (more[0] != '\0')
    {
        Put(&json, ",");
        Put(&json, more);
    }
    w->AddRoot(json.CStr());
    return w->Glb();
}

}  // namespace

TEST_CASE("gltf skin: a chain with a node between two joints folds it in, and the bind pose is the glTF's",
          "[asset][gltf]")
{
    const TestNode nodes[6] = {{"root", -1, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}},
                               {"a", 0, {0, 1, 0}, {0, 0, 0.70710678118654752, 0.70710678118654752}, {1, 1, 1}},
                               {"between", 1, {1, 0, 0}, {0, 0, 0, 1}, {2, 2, 2}},
                               {"b", 2, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}},
                               {"c", 3, {1, 2, 3}, {0, 0, 0, 1}, {1, 1, 1}},
                               {"d", 4, {0, 0, 1}, {0.70710678118654752, 0, 0, 0.70710678118654752}, {1, 1, 1}}};
    const u32 joints[5] = {0, 1, 3, 4, 5};
    GltfWriter w;
    const Vector<byte> glb = SkinGlb(&w, nodes, 6, joints, 5);
    ImportResult result;
    GltfError error;
    REQUIRE(SkImport(glb, &result, &error).Ok());
    const ImportedResource* skeleton = Find(result, types::kSkeleton);
    REQUIRE(skeleton != nullptr);
    CHECK(skeleton->path.View() == StringView("/gltf/skeletons/skin0"));
    TypeRegistry types;
    SkeletonView view;
    REQUIRE(view.Open(BlobOf(*skeleton), types).Ok());
    REQUIRE(view.JointCount() == 5);
    CHECK(view.Parent(0) == kNoParent);
    CHECK(view.Parent(2) == 1);  // `b`'s parent is `a`: the node between is folded away
    CHECK(StringView(view.Name(0)) == StringView("root"));
    const Vector<Mat> worlds = SkeletonWorlds(view);
    for (u32 j = 0; j < 5; ++j)
    {
        CHECK(MaxDifference(worlds[j], WorldOf(nodes, static_cast<i32>(joints[j]))) < 1e-5);
    }
    // The inverse bind matrix of a joint takes its world position back to the origin.
    const Array<f32, 12> ibm = view.InverseBind(4);
    const Mat world = WorldOf(nodes, 5);
    const f64 px = world.a[12];
    const f64 py = world.a[13];
    const f64 pz = world.a[14];
    CHECK(Abs(ibm[0] * px + ibm[1] * py + ibm[2] * pz + ibm[3]) < 1e-4);
    CHECK(Abs(ibm[4] * px + ibm[5] * py + ibm[6] * pz + ibm[7]) < 1e-4);
    CHECK(Abs(ibm[8] * px + ibm[9] * py + ibm[10] * pz + ibm[11]) < 1e-4);
}

TEST_CASE("gltf skin: joints listed child first are put parent first, and meshes follow", "[asset][gltf]")
{
    const TestNode nodes[3] = {{"root", -1, {0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}},
                               {"mid", 0, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}},
                               {"tip", 1, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}, 0, 0}};
    const u32 joints[3] = {2, 0, 1};  // slot 0 is the tip
    GltfWriter w;
    const f32 positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const u16 indices[3] = {0, 1, 2};
    const u8 joint_values[12] = {0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0};  // the tip, the root, the mid
    const f32 weights[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    const u32 pos = SkAcc(&w, positions, 9, 5126, "VEC3");
    const u32 idx = SkAcc(&w, indices, 3, 5123, "SCALAR");
    const u32 jnt = SkAcc(&w, joint_values, 12, 5121, "VEC4");
    const u32 wgt = SkAcc(&w, weights, 12, 5126, "VEC4");
    char mesh[300];
    FormatTo(mesh, sizeof(mesh),
             "\"meshes\":[{\"name\":\"m\",\"primitives\":[{\"attributes\":{\"POSITION\":%u,\"JOINTS_0\":%u,"
             "\"WEIGHTS_0\":%u},\"indices\":%u}]}]",
             pos, jnt, wgt, idx);
    const Vector<byte> glb = SkinGlb(&w, nodes, 3, joints, 3, "", mesh);
    ImportResult result;
    GltfError error;
    REQUIRE(SkImport(glb, &result, &error).Ok());
    TypeRegistry types;
    SkeletonView skeleton;
    MeshView mesh_view;
    REQUIRE(skeleton.Open(BlobOf(*Find(result, types::kSkeleton)), types).Ok());
    REQUIRE(mesh_view.Open(BlobOf(*Find(result, types::kMesh)), types).Ok());
    CHECK(StringView(skeleton.Name(0)) == StringView("root"));
    CHECK(StringView(skeleton.Name(1)) == StringView("mid"));
    CHECK(StringView(skeleton.Name(2)) == StringView("tip"));
    CHECK(skeleton.Parent(1) == 0);
    CHECK(skeleton.Parent(2) == 1);
    REQUIRE(mesh_view.Skinned());
    CHECK(mesh_view.Skeleton() == Find(result, types::kSkeleton)->id);
    CHECK(mesh_view.CheckSkeleton(skeleton).Ok());
    CHECK(mesh_view.CheckSkin().Ok());
    // Vertex 0 was bound to slot 0 (the tip), which is joint 2 of the skeleton; vertex 1 the root, vertex 2 the mid.
    REQUIRE(mesh_view.SkinBytes().Size() >= 24);
    const byte* skin = mesh_view.SkinBytes().Data();
    CHECK(static_cast<u32>(skin[0]) == 2);
    CHECK(static_cast<u32>(skin[8]) == 0);
    CHECK(static_cast<u32>(skin[16]) == 1);
    // The skeleton comes before the mesh that names it.
    CHECK(Find(result, types::kSkeleton) < Find(result, types::kMesh));
}

TEST_CASE("gltf skin: joint names are made, kept distinct and cut, and the renames are listed", "[asset][gltf]")
{
    char long_name[100];
    for (int i = 0; i < 99; ++i)
    {
        long_name[i] = 'n';
    }
    long_name[99] = '\0';
    const TestNode nodes[5] = {{nullptr, -1, {0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}},
                               {"dup", 0, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}},
                               {"dup", 1, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}},
                               {long_name, 2, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}},
                               {"dup_1", 3, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}}};
    const u32 joints[5] = {0, 1, 2, 3, 4};
    GltfWriter w;
    const Vector<byte> glb = SkinGlb(&w, nodes, 5, joints, 5);
    ImportResult result;
    GltfError error;
    REQUIRE(SkImport(glb, &result, &error).Ok());
    TypeRegistry types;
    SkeletonView view;
    REQUIRE(view.Open(BlobOf(*Find(result, types::kSkeleton)), types).Ok());
    CHECK(StringView(view.Name(0)) == StringView("joint_0"));
    CHECK(StringView(view.Name(1)) == StringView("dup"));
    CHECK(StringView(view.Name(2)) == StringView("dup_1"));
    CHECK(view.Name(3).Length() == kMaxJointNameBytes);
    CHECK(StringView(view.Name(4)) == StringView("dup_1_1"));  // the name was taken by the rename above
    CHECK(NoteSays(result, "skins[0].joints[2]: joint renamed to dup_1"));
    CHECK(NoteSays(result, "skins[0].joints[3]: joint renamed to "));
}

TEST_CASE("gltf skin: stored inverse bind matrices are used, and a difference is a warning", "[asset][gltf]")
{
    const TestNode nodes[2] = {{"root", -1, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}},
                               {"tip", 0, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}}};
    const u32 joints[2] = {0, 1};
    // Column-major inverse bind matrices: the first is right (translate by -1 in y), the second is off by a metre.
    const f32 good_and_bad[32] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1,  //
                                  1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1};
    f32 off[32];
    MemCopy(off, good_and_bad, sizeof(off));
    off[29] = -3.0f;  // the second matrix's y translation: the bind pose says -2
    GltfWriter w;
    const u32 ibm = SkAcc(&w, off, 32, 5126, "MAT4");
    char extra[64];
    FormatTo(extra, sizeof(extra), ",\"inverseBindMatrices\":%u", ibm);
    const Vector<byte> glb = SkinGlb(&w, nodes, 2, joints, 2, extra);
    ImportResult result;
    GltfError error;
    REQUIRE(SkImport(glb, &result, &error).Ok());
    TypeRegistry types;
    SkeletonView view;
    REQUIRE(view.Open(BlobOf(*Find(result, types::kSkeleton)), types).Ok());
    CHECK(view.InverseBind(0)[7] == -1.0f);
    CHECK(view.InverseBind(1)[7] == -3.0f);  // the stored matrix, not the computed -2
    CHECK(result.report.warnings == 1);
    CHECK(NoteSays(result, "1 inverse bind matrices differ"));

    // A matrix that is not affine, and a count that is not the joints', are refused naming the accessor.
    GltfWriter bad;
    f32 projective[32];
    MemCopy(projective, good_and_bad, sizeof(projective));
    projective[3] = 0.5f;
    const u32 bad_ibm = SkAcc(&bad, projective, 32, 5126, "MAT4");
    FormatTo(extra, sizeof(extra), ",\"inverseBindMatrices\":%u", bad_ibm);
    CHECK(SkImport(SkinGlb(&bad, nodes, 2, joints, 2, extra), &result, &error).Is(ErrorCode::Invalid));
    CHECK(SkSays(error, "skins[0].inverseBindMatrices"));
    GltfWriter few;
    const u32 few_ibm = SkAcc(&few, good_and_bad, 16, 5126, "MAT4");
    FormatTo(extra, sizeof(extra), ",\"inverseBindMatrices\":%u", few_ibm);
    CHECK(SkImport(SkinGlb(&few, nodes, 2, joints, 2, extra), &result, &error).Is(ErrorCode::Invalid));
    CHECK(SkSays(error, "different number of matrices"));
}

TEST_CASE("gltf skin: a shear in a bind transform, a repeated joint and too many joints are refused", "[asset][gltf]")
{
    // A node scaled unevenly above a rotated joint: the product has a shear.
    const TestNode sheared[2] = {{"squash", -1, {0, 0, 0}, {0, 0, 0, 1}, {1, 3, 1}},
                                 {"turn", 0, {0, 1, 0}, {0.38268343236508978, 0, 0, 0.92387953251128674}, {1, 1, 1}}};
    const u32 only_turn[1] = {1};
    GltfWriter w;
    ImportResult result;
    GltfError error;
    CHECK(SkImport(SkinGlb(&w, sheared, 2, only_turn, 1), &result, &error).Is(ErrorCode::Invalid));
    CHECK(SkSays(error, "skins[0].joints[0]"));
    CHECK(SkSays(error, "shear"));

    const TestNode two[2] = {{"a", -1, {0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}},
                             {"b", 0, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}}};
    const u32 repeated[2] = {1, 1};
    GltfWriter r;
    CHECK(SkImport(SkinGlb(&r, two, 2, repeated, 2), &result, &error).Is(ErrorCode::Invalid));
    CHECK(SkSays(error, "skins[0].joints[1]"));

    // 257 joints in a chain.
    Vector<TestNode> chain;
    Vector<u32> all;
    for (u32 i = 0; i < 257; ++i)
    {
        chain.Push(TestNode{nullptr, i == 0 ? -1 : static_cast<i32>(i - 1), {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}});
        all.Push(i);
    }
    GltfWriter big;
    CHECK(SkImport(SkinGlb(&big, chain.Data(), chain.Size(), all.Data(), all.Size()), &result, &error)
              .Is(ErrorCode::TooLarge));
    CHECK(SkSays(error, "skins[0].joints"));
    CHECK(SkSays(error, "256"));
}

TEST_CASE("gltf skin: a cycle of nodes, a node with two parents and a joint that is no node are refused",
          "[asset][gltf]")
{
    ImportResult result;
    GltfError error;
    {
        GltfWriter w;
        const u32 padding = 0;
        w.AddView(&padding, sizeof(padding));
        w.AddRoot("\"nodes\":[{\"children\":[1]},{\"children\":[0]}],\"skins\":[{\"joints\":[0]}]");
        CHECK(SkImport(w.Glb(), &result, &error).Is(ErrorCode::Invalid));
        CHECK(SkSays(error, "cycle"));
    }
    {
        GltfWriter w;
        const u32 padding = 0;
        w.AddView(&padding, sizeof(padding));
        w.AddRoot("\"nodes\":[{\"children\":[2]},{\"children\":[2]},{}],\"skins\":[{\"joints\":[0]}]");
        CHECK(SkImport(w.Glb(), &result, &error).Is(ErrorCode::Invalid));
        CHECK(SkSays(error, "another parent"));
    }
    {
        GltfWriter w;
        const u32 padding = 0;
        w.AddView(&padding, sizeof(padding));
        w.AddRoot("\"nodes\":[{}],\"skins\":[{\"joints\":[5]}]");
        CHECK(SkImport(w.Glb(), &result, &error).Is(ErrorCode::Invalid));
        CHECK(SkSays(error, "skins[0].joints[0]"));
    }
}

TEST_CASE("gltf skin: two skins with two meshes, and a mesh used with two skins", "[asset][gltf]")
{
    const TestNode nodes[4] = {{"a0", -1, {0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}, 0, 0},
                               {"a1", 0, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}},
                               {"b0", -1, {5, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}, 1, 1},
                               {"b1", 2, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}}};
    GltfWriter w;
    const f32 positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const u16 indices[3] = {0, 1, 2};
    const u8 joint_values[12] = {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    const f32 weights[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    const u32 pos = SkAcc(&w, positions, 9, 5126, "VEC3");
    const u32 idx = SkAcc(&w, indices, 3, 5123, "SCALAR");
    const u32 jnt = SkAcc(&w, joint_values, 12, 5121, "VEC4");
    const u32 wgt = SkAcc(&w, weights, 12, 5126, "VEC4");
    char prim[200];
    FormatTo(prim, sizeof(prim), "{\"attributes\":{\"POSITION\":%u,\"JOINTS_0\":%u,\"WEIGHTS_0\":%u},\"indices\":%u}",
             pos, jnt, wgt, idx);
    String<HeapAllocator> json = NodesJson(nodes, 4);
    Put(&json, ",\"skins\":[{\"name\":\"left\",\"joints\":[0,1]},{\"name\":\"right\",\"joints\":[2,3]}],");
    Fmt(&json, "\"meshes\":[{\"name\":\"l\",\"primitives\":[%s]},{\"name\":\"r\",\"primitives\":[%s]}]", prim, prim);
    w.AddRoot(json.CStr());
    const Vector<byte> glb = w.Glb();
    ImportResult result;
    GltfError error;
    REQUIRE(SkImport(glb, &result, &error).Ok());
    const ImportedResource* left = Find(result, types::kSkeleton, 0);
    const ImportedResource* right = Find(result, types::kSkeleton, 1);
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);
    CHECK(left->path.View() == StringView("/gltf/skeletons/left"));
    TypeRegistry types;
    MeshView l;
    MeshView r;
    REQUIRE(l.Open(BlobOf(*Find(result, types::kMesh, 0)), types).Ok());
    REQUIRE(r.Open(BlobOf(*Find(result, types::kMesh, 1)), types).Ok());
    CHECK(l.Skeleton() == left->id);
    CHECK(r.Skeleton() == right->id);

    // The same mesh on a node of each skin: refused.
    const TestNode clash[4] = {{"a0", -1, {0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}, 0, 0},
                               {"a1", 0, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}},
                               {"b0", -1, {5, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}, 0, 1},
                               {"b1", 2, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}}};
    GltfWriter c;
    const u32 pos2 = SkAcc(&c, positions, 9, 5126, "VEC3");
    const u32 idx2 = SkAcc(&c, indices, 3, 5123, "SCALAR");
    const u32 jnt2 = SkAcc(&c, joint_values, 12, 5121, "VEC4");
    const u32 wgt2 = SkAcc(&c, weights, 12, 5126, "VEC4");
    FormatTo(prim, sizeof(prim), "{\"attributes\":{\"POSITION\":%u,\"JOINTS_0\":%u,\"WEIGHTS_0\":%u},\"indices\":%u}",
             pos2, jnt2, wgt2, idx2);
    String<HeapAllocator> clash_json = NodesJson(clash, 4);
    Put(&clash_json, ",\"skins\":[{\"joints\":[0,1]},{\"joints\":[2,3]}],");
    Fmt(&clash_json, "\"meshes\":[{\"primitives\":[%s]}]", prim);
    c.AddRoot(clash_json.CStr());
    CHECK(SkImport(c.Glb(), &result, &error).Is(ErrorCode::Invalid));
    CHECK(SkSays(error, "meshes[0]"));
    CHECK(SkSays(error, "two skins"));
}

TEST_CASE("gltf clip: a joint chain animated in all three channels plays back within the clip's declared bounds",
          "[asset][gltf]")
{
    // 20 joints in a chain, each with translation, rotation and scale channels of 8 keys.
    constexpr u32 kJoints = 20;
    constexpr u32 kKeys = 8;
    Vector<TestNode> nodes;
    Vector<u32> joints;
    for (u32 i = 0; i < kJoints; ++i)
    {
        nodes.Push(TestNode{nullptr, i == 0 ? -1 : static_cast<i32>(i - 1), {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}});
        joints.Push(i);
    }
    f32 times[kKeys];
    for (u32 k = 0; k < kKeys; ++k)
    {
        times[k] = 0.25f * static_cast<f32>(k) + (k % 3 == 1 ? 0.05f : 0.0f);  // not on a grid
    }
    Vector<f32> translations;
    Vector<f32> rotations;
    Vector<f32> scales;
    u64 state = 12345;
    auto next = [&state]() -> f32
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<f32>((state >> 33) & 0xffff) / 65535.0f;
    };
    for (u32 j = 0; j < kJoints; ++j)
    {
        f64 angle = 0.0;
        for (u32 k = 0; k < kKeys; ++k)
        {
            translations.Push(next() * 4.0f - 2.0f);
            translations.Push(next() * 4.0f - 2.0f);
            translations.Push(next() * 4.0f - 2.0f);
            angle += static_cast<f64>(next()) * 0.8 - 0.4;  // radians; consecutive keys stay within 23 degrees
            rotations.Push(static_cast<f32>(Sin(angle * 0.5) * 0.6));
            rotations.Push(static_cast<f32>(Sin(angle * 0.5) * 0.8));
            rotations.Push(0.0f);
            rotations.Push(static_cast<f32>(Cos(angle * 0.5)));
            scales.Push(0.5f + next());
            scales.Push(0.5f + next());
            scales.Push(0.5f + next());
        }
    }
    GltfWriter w;
    const u32 time_accessor = SkAcc(&w, times, kKeys, 5126, "SCALAR");
    String<HeapAllocator> animation(HeapAllocator::Default(),
                                    StringView("\"animations\":[{\"name\":\"walk\",\"channels\":["));
    String<HeapAllocator> samplers(HeapAllocator::Default());
    u32 sampler = 0;
    for (u32 j = 0; j < kJoints; ++j)
    {
        const u32 t = SkAcc(&w, translations.Data() + j * kKeys * 3, kKeys * 3, 5126, "VEC3");
        const u32 r = SkAcc(&w, rotations.Data() + j * kKeys * 4, kKeys * 4, 5126, "VEC4");
        const u32 s = SkAcc(&w, scales.Data() + j * kKeys * 3, kKeys * 3, 5126, "VEC3");
        const char* paths[3] = {"translation", "rotation", "scale"};
        const u32 outputs[3] = {t, r, s};
        for (u32 p = 0; p < 3; ++p)
        {
            Fmt(&samplers, "%s{\"input\":%u,\"output\":%u,\"interpolation\":\"LINEAR\"}", sampler == 0 ? "" : ",",
                time_accessor, outputs[p]);
            Fmt(&animation, "%s{\"sampler\":%u,\"target\":{\"node\":%u,\"path\":\"%s\"}}", sampler == 0 ? "" : ",",
                sampler, j, paths[p]);
            ++sampler;
        }
    }
    Put(&animation, "],\"samplers\":[");
    animation.Append(samplers.View());
    Put(&animation, "]}]");
    GltfWriter& writer = w;
    String<HeapAllocator> root = NodesJson(nodes.Data(), nodes.Size());
    Put(&root, ",\"skins\":[{\"name\":\"rig\",\"joints\":[");
    for (u32 j = 0; j < kJoints; ++j)
    {
        Fmt(&root, "%s%u", j == 0 ? "" : ",", j);
    }
    Put(&root, "]}],");
    root.Append(animation.View());
    writer.AddRoot(root.CStr());
    const Vector<byte> glb = writer.Glb();
    ImportResult result;
    GltfError error;
    REQUIRE(SkImport(glb, &result, &error).Ok());
    const ImportedResource* clip_resource = Find(result, types::kAnimation);
    REQUIRE(clip_resource != nullptr);
    CHECK(clip_resource->path.View() == StringView("/gltf/clips/walk_rig"));
    TypeRegistry types;
    ClipView clip;
    SkeletonView skeleton;
    REQUIRE(clip.Open(BlobOf(*clip_resource), types).Ok());
    REQUIRE(skeleton.Open(BlobOf(*Find(result, types::kSkeleton)), types).Ok());
    CHECK(clip.Bind(skeleton).Ok());
    CHECK(clip.TrackCount() == kJoints);
    CHECK(Abs(clip.Duration() - times[kKeys - 1]) < 1e-3f);
    CHECK_FALSE(clip.Looping());

    // 1,000 random times, against the glTF's keys interpolated in doubles (linear, rotations normalized).
    const f64 rotation_limit = static_cast<f64>(clip.RotationError()) + 0.05;  // degrees
    f64 worst_rotation = 0;
    f64 worst_value = 0;
    for (u32 n = 0; n < 1000; ++n)
    {
        const f64 time = static_cast<f64>(next()) * static_cast<f64>(times[kKeys - 1]);
        const u32 joint = static_cast<u32>(next() * static_cast<f32>(kJoints)) % kJoints;
        u32 track = 0;
        while (track < clip.TrackCount() && clip.TrackJoint(track) != joint)
        {
            ++track;
        }
        REQUIRE(track < clip.TrackCount());
        const ClipSample sample = clip.Sample(track, static_cast<f32>(time));
        u32 k = 0;
        while (k + 2 < kKeys && time > static_cast<f64>(times[k + 1]))
        {
            ++k;
        }
        const f64 u = (time - static_cast<f64>(times[k])) / static_cast<f64>(times[k + 1] - times[k]);
        for (u32 c = 0; c < 3; ++c)
        {
            const f64 t0 = static_cast<f64>(translations[(joint * kKeys + k) * 3 + c]);
            const f64 t1 = static_cast<f64>(translations[(joint * kKeys + k + 1) * 3 + c]);
            const f64 s0 = static_cast<f64>(scales[(joint * kKeys + k) * 3 + c]);
            const f64 s1 = static_cast<f64>(scales[(joint * kKeys + k + 1) * 3 + c]);
            worst_value = Worse(worst_value, Abs(static_cast<f64>(sample.translation[c]) - (t0 + (t1 - t0) * u)));
            worst_value = Worse(worst_value, Abs(static_cast<f64>(sample.scale[c]) - (s0 + (s1 - s0) * u)));
        }
        f64 q0[4];
        f64 q1[4];
        f64 q[4];
        for (u32 c = 0; c < 4; ++c)
        {
            q0[c] = static_cast<f64>(rotations[(joint * kKeys + k) * 4 + c]);
            q1[c] = static_cast<f64>(rotations[(joint * kKeys + k + 1) * 4 + c]);
        }
        const f64 dot = q0[0] * q1[0] + q0[1] * q1[1] + q0[2] * q1[2] + q0[3] * q1[3];
        f64 length = 0;
        for (u32 c = 0; c < 4; ++c)
        {
            q[c] = q0[c] * (1 - u) + (dot < 0 ? -q1[c] : q1[c]) * u;
            length += q[c] * q[c];
        }
        length = Sqrt(length);
        f64 cosine = 0;
        for (u32 c = 0; c < 4; ++c)
        {
            cosine += q[c] / length * static_cast<f64>(sample.rotation[c]);
        }
        cosine = Abs(cosine) > 1.0 ? 1.0 : Abs(cosine);
        worst_rotation = Worse(worst_rotation, 2.0 * Acos(cosine) * 180.0 / 3.14159265358979323846);
    }
    CHECK(worst_rotation <= rotation_limit);
    CHECK(worst_value <= static_cast<f64>(clip.TranslationError()) * 4.0 + 1e-4);
}

TEST_CASE("gltf clip: STEP holds the value until the step, and a cubic spline is resampled within its recorded error",
          "[asset][gltf]")
{
    const TestNode nodes[1] = {{"j", -1, {0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}}};
    const u32 joints[1] = {0};
    // STEP: x = 0 until t = 1, then 1 until t = 2, then 2.
    const f32 times[3] = {0, 1, 2};
    const f32 steps[9] = {0, 0, 0, 1, 0, 0, 2, 0, 0};
    // CUBICSPLINE: in tangent, value, out tangent per key; x from 0 to 4 with tangents of 4 (a straight run) and then
    // 0 (an ease out), three keys at 0, 1 and 3 seconds.
    const f32 spline_times[3] = {0, 1, 3};
    const f32 spline[27] = {0, 0, 0, 0, 0, 0, 4, 0, 0,  //
                            4, 0, 0, 3, 1, 0, 0, 2, 0,  //
                            0, 0, 0, 5, 0, 0, 0, 0, 0};
    GltfWriter w;
    const u32 time_accessor = SkAcc(&w, times, 3, 5126, "SCALAR");
    const u32 step_accessor = SkAcc(&w, steps, 9, 5126, "VEC3");
    const u32 spline_time_accessor = SkAcc(&w, spline_times, 3, 5126, "SCALAR");
    const u32 spline_accessor = SkAcc(&w, spline, 27, 5126, "VEC3");
    char animations[900];
    FormatTo(animations, sizeof(animations),
             "\"animations\":[{\"name\":\"step\",\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":"
             "\"translation\"}}],\"samplers\":[{\"input\":%u,\"output\":%u,\"interpolation\":\"STEP\"}]},"
             "{\"name\":\"curve\",\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}}],"
             "\"samplers\":[{\"input\":%u,\"output\":%u,\"interpolation\":\"CUBICSPLINE\"}]}]",
             time_accessor, step_accessor, spline_time_accessor, spline_accessor);
    const Vector<byte> glb = SkinGlb(&w, nodes, 1, joints, 1, "", animations);
    ImportResult result;
    GltfError error;
    REQUIRE(SkImport(glb, &result, &error).Ok());
    TypeRegistry types;
    ClipView step;
    ClipView curve;
    REQUIRE(step.Open(BlobOf(*Find(result, types::kAnimation, 0)), types).Ok());
    REQUIRE(curve.Open(BlobOf(*Find(result, types::kAnimation, 1)), types).Ok());
    CHECK(Abs(step.Sample(0, 0.5f).translation[0] - 0.0f) < 1e-3f);
    CHECK(Abs(step.Sample(0, 0.98f).translation[0] - 0.0f) < 1e-3f);
    CHECK(Abs(step.Sample(0, 1.5f).translation[0] - 1.0f) < 1e-3f);
    CHECK(Abs(step.Sample(0, 2.5f).translation[0] - 2.0f) < 0.05f);
    const f32 halfway = step.Sample(0, 1.0f + 0.5f / 30.0f).translation[0];
    CHECK(halfway > 0.2f);  // between the held value and the new one inside the one-tick step
    CHECK(halfway < 0.8f);

    // The spline: the recorded resampling error bounds the playback error at random times.
    const f64 recorded = static_cast<f64>(result.report.resampling_translation_error);
    CHECK(result.report.resampling_translation_error >= 0.0f);
    CHECK(NoteSays(result, "cubic-spline channels resampled at 30 Hz"));
    u64 state = 99;
    for (u32 n = 0; n < 1000; ++n)
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const f64 time = static_cast<f64>((state >> 33) & 0xffff) / 65535.0 * 3.0;
        const u32 k = time < 1.0 ? 0 : 1;
        const f64 t0 = k == 0 ? 0.0 : 1.0;
        const f64 dt = k == 0 ? 1.0 : 2.0;
        const f64 s = (time - t0) / dt;
        const f64 s2 = s * s;
        const f64 s3 = s2 * s;
        const f64 p0 = k == 0 ? 0.0 : 3.0;
        const f64 p1 = k == 0 ? 3.0 : 5.0;
        const f64 m0 = k == 0 ? 4.0 : 0.0;  // the out tangent of key k, in x
        const f64 m1 = k == 0 ? 4.0 : 0.0;  // the in tangent of key k + 1
        const f64 exact = (2 * s3 - 3 * s2 + 1) * p0 + dt * (s3 - 2 * s2 + s) * m0 + (-2 * s3 + 3 * s2) * p1 +
                          dt * (s3 - s2) * m1;
        const f64 played = static_cast<f64>(curve.Sample(0, static_cast<f32>(time)).translation[0]);
        CHECK(Abs(played - exact) <= recorded * 1.5 + static_cast<f64>(curve.TranslationError()) * 6.0 + 1e-4);
    }
}

TEST_CASE("gltf clip: channels that cannot play are dropped with a warning, and a looping clip is marked",
          "[asset][gltf]")
{
    const TestNode nodes[3] = {{"j", -1, {0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}},
                               {"outside", -1, {0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}},
                               {"k", 0, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}}};
    const u32 joints[2] = {0, 2};
    const f32 times[2] = {0, 1};
    const f32 values[6] = {0, 0, 0, 1, 0, 0};
    const f32 weights[2] = {0, 1};
    GltfWriter w;
    const u32 time_accessor = SkAcc(&w, times, 2, 5126, "SCALAR");
    const u32 value_accessor = SkAcc(&w, values, 6, 5126, "VEC3");
    const u32 weight_accessor = SkAcc(&w, weights, 2, 5126, "SCALAR");
    char animations[900];
    FormatTo(animations, sizeof(animations),
             "\"animations\":[{\"name\":\"loop me\",\"channels\":["
             "{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}},"
             "{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}},"
             "{\"sampler\":1,\"target\":{\"node\":2,\"path\":\"weights\"}}],"
             "\"samplers\":[{\"input\":%u,\"output\":%u},{\"input\":%u,\"output\":%u}]}]",
             time_accessor, value_accessor, time_accessor, weight_accessor);
    const Vector<byte> glb = SkinGlb(&w, nodes, 3, joints, 2, "", animations);
    ImportResult result;
    GltfError error;
    const StringView loops[1] = {StringView("loop me")};
    ImportOptions options;
    options.looping_clips = Span<const StringView>(loops, 1);
    REQUIRE(SkImport(glb, &result, &error, options).Ok());
    CHECK(result.report.warnings == 2);  // the channel on a node outside the skin, and the weights channel
    CHECK(NoteSays(result, "animations[0].channels[1]"));
    CHECK(NoteSays(result, "animations[0].channels[2]"));
    TypeRegistry types;
    ClipView clip;
    REQUIRE(clip.Open(BlobOf(*Find(result, types::kAnimation)), types).Ok());
    CHECK(clip.Looping());
    CHECK(clip.TrackCount() == 1);
    CHECK(Find(result, types::kAnimation)->path.View() == StringView("/gltf/clips/loop me_skin0"));
    ImportResult plain;
    REQUIRE(SkImport(glb, &plain, &error).Ok());
    ClipView not_looping;
    REQUIRE(not_looping.Open(BlobOf(*Find(plain, types::kAnimation)), types).Ok());
    CHECK_FALSE(not_looping.Looping());
}

TEST_CASE("gltf clip: hostile animations are refused, naming the sampler or channel", "[asset][gltf]")
{
    struct Case
    {
        const char* animation;  // the members after "name"
        const char* says;
        ErrorCode code;
    };
    // Accessors: 0 times {0, 1, 2}, 1 vec3 x3, 2 vec3 x2, 3 times {0, 2, 1}, 4 times with a NaN, 5 vec4 x3 zero
    // quaternions
    const Case cases[] = {
        {"\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}}],"
         "\"samplers\":[{\"input\":0,\"output\":2}]",
         "animations[0].samplers[0]", ErrorCode::Invalid},
        {"\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}}],"
         "\"samplers\":[{\"input\":3,\"output\":1}]",
         "samplers[0].input", ErrorCode::Invalid},
        {"\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}}],"
         "\"samplers\":[{\"input\":4,\"output\":1}]",
         "accessors[4]", ErrorCode::Invalid},
        {"\"channels\":[{\"sampler\":0,\"target\":{\"node\":9,\"path\":\"translation\"}}],"
         "\"samplers\":[{\"input\":0,\"output\":1}]",
         "channels[0].target.node", ErrorCode::Invalid},
        {"\"channels\":[{\"sampler\":3,\"target\":{\"node\":0,\"path\":\"translation\"}}],"
         "\"samplers\":[{\"input\":0,\"output\":1}]",
         "channels[0].sampler", ErrorCode::Invalid},
        {"\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}},"
         "{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}}],"
         "\"samplers\":[{\"input\":0,\"output\":1}]",
         "two channels", ErrorCode::Invalid},
        {"\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}}],"
         "\"samplers\":[{\"input\":0,\"output\":1,\"interpolation\":\"BOUNCE\"}]",
         "interpolation", ErrorCode::Invalid},
        {"\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"rotation\"}}],"
         "\"samplers\":[{\"input\":0,\"output\":5}]",
         "rotation of length zero", ErrorCode::Invalid},
        {"\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"rotation\"}}],"
         "\"samplers\":[{\"input\":0,\"output\":1}]",
         "samplers[0].output", ErrorCode::Invalid},
        {"\"channels\":[],\"samplers\":[]", "animations[0]", ErrorCode::Invalid},
    };
    for (const Case& c : cases)
    {
        const TestNode nodes[1] = {{"j", -1, {0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}}};
        const u32 joints[1] = {0};
        const f32 times[3] = {0, 1, 2};
        const f32 values3[9] = {0, 0, 0, 1, 0, 0, 2, 0, 0};
        const f32 values2[6] = {0, 0, 0, 1, 0, 0};
        const f32 unordered[3] = {0, 2, 1};
        u32 nan_word = 0x7fc00000u;
        f32 nan_value;
        MemCopy(&nan_value, &nan_word, 4);
        const f32 with_nan[3] = {0, nan_value, 2};
        const f32 zero_quaternions[12] = {};
        GltfWriter w;
        SkAcc(&w, times, 3, 5126, "SCALAR");
        SkAcc(&w, values3, 9, 5126, "VEC3");
        SkAcc(&w, values2, 6, 5126, "VEC3");
        SkAcc(&w, unordered, 3, 5126, "SCALAR");
        SkAcc(&w, with_nan, 3, 5126, "SCALAR");
        SkAcc(&w, zero_quaternions, 12, 5126, "VEC4");
        String<HeapAllocator> more(HeapAllocator::Default(), StringView("\"animations\":[{"));
        Put(&more, c.animation);
        Put(&more, "}]");
        const Vector<byte> glb = SkinGlb(&w, nodes, 1, joints, 1, "", more.CStr());
        ImportResult result;
        GltfError error;
        NoGltfSource none;
        const Status status = ImportGltf(ByteSpan(glb.Data(), glb.Size()), none, ImportOptions(), &result, &error);
        INFO(c.animation);
        INFO(GltfMessage(error, status));
        CHECK(status.Is(c.code));
        CHECK(SkSays(error, c.says));
    }
}

TEST_CASE("gltf clip: skeletons and clips are deterministic and keep their ids when the file is imported again",
          "[asset][gltf]")
{
    const TestNode nodes[2] = {{"root", -1, {0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}},
                               {"tip", 0, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}}};
    const u32 joints[2] = {0, 1};
    const f32 times[2] = {0, 1};
    const f32 values[6] = {0, 0, 0, 0, 2, 0};
    GltfWriter w;
    const u32 t = SkAcc(&w, times, 2, 5126, "SCALAR");
    const u32 v = SkAcc(&w, values, 6, 5126, "VEC3");
    char animations[300];
    FormatTo(animations, sizeof(animations),
             "\"animations\":[{\"name\":\"rise\",\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":"
             "\"translation\"}}],\"samplers\":[{\"input\":%u,\"output\":%u}]}]",
             t, v);
    const Vector<byte> glb = SkinGlb(&w, nodes, 2, joints, 2, "", animations);
    ImportResult one;
    ImportResult two;
    GltfError error;
    REQUIRE(SkImport(glb, &one, &error).Ok());
    REQUIRE(SkImport(glb, &two, &error).Ok());
    REQUIRE(one.resources.Size() == 2);
    for (usize i = 0; i < 2; ++i)
    {
        CHECK(one.resources[i].id == two.resources[i].id);
        CHECK(SkSame(one.resources[i].blob, two.resources[i].blob));
    }
    ImportOptions options;
    options.manifest = &one.manifest;
    SeededIdSource fresh(5);
    options.ids = &fresh;
    ImportResult again;
    REQUIRE(SkImport(glb, &again, &error, options).Ok());
    CHECK(again.resources[0].id == one.resources[0].id);
    CHECK(again.resources[1].id == one.resources[1].id);
    CHECK(SkSame(again.resources[0].blob, one.resources[0].blob));
}

TEST_CASE("gltf skin: review findings: scale, folded ancestors, limits and long node chains", "[asset][gltf]")
{
    TypeRegistry types;
    // The import scale reaches the skeleton (translations, inverse bind translations, bounds) and the clips.
    {
        const TestNode nodes[2] = {{"root", -1, {0, 100, 0}, {0, 0, 0, 1}, {1, 1, 1}},
                                   {"tip", 0, {0, 100, 0}, {0, 0, 0, 1}, {1, 1, 1}}};
        const u32 joints[2] = {0, 1};
        const f32 times[2] = {0, 1};
        const f32 values[6] = {0, 100, 0, 0, 300, 0};
        GltfWriter w;
        const u32 t = SkAcc(&w, times, 2, 5126, "SCALAR");
        const u32 v = SkAcc(&w, values, 6, 5126, "VEC3");
        char animations[300];
        FormatTo(animations, sizeof(animations),
                 "\"animations\":[{\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}}],"
                 "\"samplers\":[{\"input\":%u,\"output\":%u}]}]",
                 t, v);
        const Vector<byte> glb = SkinGlb(&w, nodes, 2, joints, 2, "", animations);
        ImportOptions options;
        options.scale = 0.01f;
        ImportResult result;
        GltfError error;
        REQUIRE(SkImport(glb, &result, &error, options).Ok());
        SkeletonView skeleton;
        ClipView clip;
        REQUIRE(skeleton.Open(BlobOf(*Find(result, types::kSkeleton)), types).Ok());
        REQUIRE(clip.Open(BlobOf(*Find(result, types::kAnimation)), types).Ok());
        CHECK(Abs(skeleton.Translation(1)[1] - 1.0f) < 1e-5f);
        CHECK(Abs(skeleton.InverseBind(1)[7] + 2.0f) < 1e-4f);  // the tip's world y is 2 metres
        CHECK(Abs(skeleton.BoundRadius() - skeleton.BoundRadius()) < 1e-6f);
        CHECK(Abs(clip.Sample(0, 1.0f).translation[1] - 3.0f) < 0.01f);
        CHECK(Abs(clip.BoundsMax()[1] - 2.0f) < 1e-4f);
    }
    // A joint under a node that is no joint (an armature, turned and doubled): the clip follows the folded transform.
    {
        const TestNode nodes[2] = {
            {"armature", -1, {1, 0, 0}, {0, 0, 0.70710678118654752, 0.70710678118654752}, {2, 2, 2}},
            {"root", 0, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}}};
        const u32 joints[1] = {1};
        const f32 times[2] = {0, 1};
        const f32 translations[6] = {0, 1, 0, 0, 3, 0};
        const f32 rotations[8] = {0, 0, 0, 1, 0, 0, 0, 1};
        GltfWriter w;
        const u32 t = SkAcc(&w, times, 2, 5126, "SCALAR");
        const u32 tv = SkAcc(&w, translations, 6, 5126, "VEC3");
        const u32 rv = SkAcc(&w, rotations, 8, 5126, "VEC4");
        char animations[500];
        FormatTo(animations, sizeof(animations),
                 "\"animations\":[{\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}},"
                 "{\"sampler\":1,\"target\":{\"node\":1,\"path\":\"rotation\"}}],"
                 "\"samplers\":[{\"input\":%u,\"output\":%u},{\"input\":%u,\"output\":%u}]}]",
                 t, tv, t, rv);
        const Vector<byte> glb = SkinGlb(&w, nodes, 2, joints, 1, "", animations);
        ImportResult result;
        GltfError error;
        REQUIRE(SkImport(glb, &result, &error).Ok());
        SkeletonView skeleton;
        ClipView clip;
        REQUIRE(skeleton.Open(BlobOf(*Find(result, types::kSkeleton)), types).Ok());
        REQUIRE(clip.Open(BlobOf(*Find(result, types::kAnimation)), types).Ok());
        // At t = 0 the clip is the bind pose: (1,0,0) + turn(2 * (0,1,0)) = (-1,0,0), turned a quarter, doubled.
        const ClipSample start = clip.Sample(0, 0.0f);
        CHECK(Abs(start.translation[0] - skeleton.Translation(0)[0]) < 1e-4f);
        CHECK(Abs(start.translation[1] - skeleton.Translation(0)[1]) < 1e-4f);
        CHECK(Abs(skeleton.Translation(0)[0] + 1.0f) < 1e-4f);
        const ClipSample end = clip.Sample(0, 1.0f);
        CHECK(Abs(end.translation[0] + 5.0f) < 1e-3f);  // (1,0,0) + turn(2 * (0,3,0))
        CHECK(Abs(end.translation[1]) < 1e-3f);
        CHECK(Abs(Abs(end.rotation[2]) - 0.70710678f) < 1e-3f);  // the armature's quarter turn, kept
        CHECK(Abs(Abs(end.rotation[3]) - 0.70710678f) < 1e-3f);
    }
    // An armature with an uneven scale cannot follow an animated rotation: the channel is dropped, with a warning.
    {
        const TestNode nodes[2] = {{"armature", -1, {0, 0, 0}, {0, 0, 0, 1}, {1, 3, 1}},
                                   {"root", 0, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}}};
        const u32 joints[1] = {1};
        const f32 times[2] = {0, 1};
        const f32 rotations[8] = {0, 0, 0, 1, 0, 0, 0.70710678f, 0.70710678f};
        GltfWriter w;
        const u32 t = SkAcc(&w, times, 2, 5126, "SCALAR");
        const u32 rv = SkAcc(&w, rotations, 8, 5126, "VEC4");
        char animations[300];
        FormatTo(animations, sizeof(animations),
                 "\"animations\":[{\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"rotation\"}}],"
                 "\"samplers\":[{\"input\":%u,\"output\":%u}]}]",
                 t, rv);
        const Vector<byte> glb = SkinGlb(&w, nodes, 2, joints, 1, "", animations);
        ImportResult result;
        GltfError error;
        REQUIRE(SkImport(glb, &result, &error).Ok());
        CHECK(Find(result, types::kAnimation) == nullptr);
        CHECK(result.report.warnings == 1);
        CHECK(NoteSays(result, "shear or an uneven scale"));
    }
    // One output limit for the skeletons, meshes and clips together.
    {
        const TestNode nodes[2] = {{"root", -1, {0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}, 0, 0},
                                   {"tip", 0, {0, 1, 0}, {0, 0, 0, 1}, {1, 1, 1}}};
        const u32 joints[2] = {0, 1};
        GltfWriter w;
        const f32 positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        const u16 indices[3] = {0, 1, 2};
        const u8 joint_values[12] = {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
        const f32 weights[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
        const u32 pos = SkAcc(&w, positions, 9, 5126, "VEC3");
        const u32 idx = SkAcc(&w, indices, 3, 5123, "SCALAR");
        const u32 jnt = SkAcc(&w, joint_values, 12, 5121, "VEC4");
        const u32 wgt = SkAcc(&w, weights, 12, 5126, "VEC4");
        char mesh[300];
        FormatTo(mesh, sizeof(mesh),
                 "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%u,\"JOINTS_0\":%u,\"WEIGHTS_0\":%u},"
                 "\"indices\":%u}]}]",
                 pos, jnt, wgt, idx);
        const Vector<byte> glb = SkinGlb(&w, nodes, 2, joints, 2, "", mesh);
        ImportResult all;
        GltfError error;
        REQUIRE(SkImport(glb, &all, &error).Ok());
        REQUIRE(all.resources.Size() == 2);
        usize biggest = 0;
        for (usize i = 0; i < all.resources.Size(); ++i)
        {
            biggest = all.resources[i].blob.Size() > biggest ? all.resources[i].blob.Size() : biggest;
        }
        ImportOptions options;
        options.limits.max_output_bytes = biggest + 1;  // each blob fits alone; both do not
        ImportResult limited;
        CHECK(SkImport(glb, &limited, &error, options).Is(ErrorCode::TooLarge));
        CHECK(all.report.output_bytes == all.resources[0].blob.Size() + all.resources[1].blob.Size());
    }
    // A STEP sampler that doubles past the key limit is refused by name.
    {
        const TestNode nodes[1] = {{"j", -1, {0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}}};
        const u32 joints[1] = {0};
        Vector<f32> times;
        Vector<f32> values;
        for (u32 i = 0; i < 40000; ++i)
        {
            times.Push(static_cast<f32>(i));
            values.Push(0);
            values.Push(0);
            values.Push(0);
        }
        GltfWriter w;
        const u32 t = SkAcc(&w, times.Data(), times.Size(), 5126, "SCALAR");
        const u32 v = SkAcc(&w, values.Data(), values.Size(), 5126, "VEC3");
        char animations[300];
        FormatTo(animations, sizeof(animations),
                 "\"animations\":[{\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}}],"
                 "\"samplers\":[{\"input\":%u,\"output\":%u,\"interpolation\":\"STEP\"}]}]",
                 t, v);
        ImportResult result;
        GltfError error;
        CHECK(SkImport(SkinGlb(&w, nodes, 1, joints, 1, "", animations), &result, &error).Is(ErrorCode::TooLarge));
        CHECK(SkSays(error, "animations[0].samplers[0]"));
    }
    // A scale that overflows a float is refused naming the joint.
    {
        const TestNode nodes[1] = {{"huge", -1, {0, 0, 0}, {0, 0, 0, 1}, {1e300, 1, 1}}};
        const u32 joints[1] = {0};
        GltfWriter w;
        ImportResult result;
        GltfError error;
        CHECK(SkImport(SkinGlb(&w, nodes, 1, joints, 1), &result, &error).Is(ErrorCode::Invalid));
        CHECK(SkSays(error, "skins[0].joints[0]"));
    }
    // A chain of 150,000 nodes (the cycle check walks each node once).
    {
        String<HeapAllocator> json(HeapAllocator::Default(), StringView("\"nodes\":["));
        constexpr u32 kChain = 150000;
        for (u32 i = 0; i < kChain; ++i)
        {
            if (i + 1 < kChain)
            {
                Fmt(&json, "%s{\"children\":[%u]}", i == 0 ? "" : ",", i + 1);
            }
            else
            {
                Put(&json, ",{}");
            }
        }
        Put(&json, "]");
        GltfWriter w;
        const u32 padding = 0;
        w.AddView(&padding, sizeof(padding));
        w.AddRoot(json.CStr());
        ImportResult result;
        GltfError error;
        const u64 begin = os::MonotonicTimeNanos();
        CHECK(SkImport(w.Glb(), &result, &error).Ok());
        CHECK(os::MonotonicTimeNanos() - begin < u64{20} * 1000 * 1000 * 1000);
        // And a cycle at its far end is found.
        String<HeapAllocator> cyclic(HeapAllocator::Default(), StringView("\"nodes\":["));
        for (u32 i = 0; i < kChain; ++i)
        {
            Fmt(&cyclic, "%s{\"children\":[%u]}", i == 0 ? "" : ",", (i + 1) % kChain);
        }
        Put(&cyclic, "]");
        GltfWriter c;
        c.AddView(&padding, sizeof(padding));
        c.AddRoot(cyclic.CStr());
        CHECK(SkImport(c.Glb(), &result, &error).Is(ErrorCode::Invalid));
        CHECK(SkSays(error, "cycle"));
    }
}

TEST_CASE("gltf skin: a file of 190,000 accessors is read in linear time", "[asset][gltf]")
{
    GltfWriter w;
    const f32 value = 1.0f;
    const u32 view = w.AddView(&value, sizeof(value));
    for (u32 i = 0; i < 190000; ++i)
    {
        w.AddAccessor(view, 5126, 1, "SCALAR");
    }
    ImportResult result;
    GltfError error;
    const u64 begin = os::MonotonicTimeNanos();
    CHECK(SkImport(w.Glb(), &result, &error).Ok());
    CHECK(os::MonotonicTimeNanos() - begin < u64{15} * 1000 * 1000 * 1000);
    CHECK(result.report.accessors == 190000);
}

}  // namespace engine_placeholder::asset
