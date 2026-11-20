// Collision from an imported glTF (glTF colliders Days 3 and 4): node-name suffixes and OMI_physics_*, the MCOLs, the
// static_collider level component, the limits, the warnings and the summary.

#include <catch2/catch_test_macros.hpp>

#include "animation/components.h"
#include "asset/level.h"
#include "asset/mesh_collider.h"
#include "asset_import/gltf_import.h"
#include "asset_import/import_settings.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "ecs/query.h"
#include "gltf_writer.h"
#include "lang/limits.h"
#include "scene/scene_components.h"
#include "settings/registry.h"
#include "storage/builtin_components.h"
#include "support.h"

namespace engine_placeholder::asset
{
namespace
{

using test::GltfWriter;

// A 2 m quad in XZ (two triangles), and a degenerate triangle after it (vertex 4 repeats vertex 0's place).
const f32 kQuad[15] = {-1, 0, -1, 1, 0, -1, 1, 0, 1, -1, 0, 1, -1, 0, -1};
const u16 kQuadIndices[9] = {0, 2, 1, 0, 3, 2, 0, 4, 1};

// A file of the quad mesh (`mesh_name`), with `nodes_json` (a `"nodes":[...]` list naming mesh 0) and a scene of the
// first `roots` nodes. `with_degenerate` keeps the third triangle.
Vector<byte> CollisionGlb(const char* nodes_json, u32 roots, bool with_degenerate = false,
                          const char* mesh_name = "slab", const char* extra = "")
{
    GltfWriter w;
    const u32 pos_view = w.AddView(kQuad, sizeof(kQuad));
    const u32 idx_view = w.AddView(kQuadIndices, with_degenerate ? sizeof(kQuadIndices) : 6 * sizeof(u16));
    w.AddAccessor(pos_view, 5126, 5, "VEC3", ",\"min\":[-1,0,-1],\"max\":[1,0,1]");
    w.AddAccessor(idx_view, 5123, with_degenerate ? 9 : 6, "SCALAR");
    char scene[256];
    usize at = static_cast<usize>(FormatTo(scene, sizeof(scene), "\"scenes\":[{\"nodes\":["));
    for (u32 r = 0; r < roots; ++r)
    {
        at += static_cast<usize>(FormatTo(scene + at, sizeof(scene) - at, "%s%u", r == 0 ? "" : ",", r));
    }
    FormatTo(scene + at, sizeof(scene) - at, "]}],\"scene\":0");
    char json[4096];
    FormatTo(json, sizeof(json),
             "\"meshes\":[{\"name\":\"%s\",\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],%s,%s%s",
             mesh_name, nodes_json, scene, extra);
    w.AddRoot(json);
    return w.Glb();
}

Status Import(const Vector<byte>& glb, ImportResult* result, GltfError* error, ImportOptions options = ImportOptions())
{
    NoGltfSource none;
    SeededIdSource ids(11);
    if (options.ids == nullptr)
    {
        options.ids = &ids;
    }
    options.emit_level = true;
    const Status status = ImportGltf(ByteSpan(glb.Data(), glb.Size()), none, options, result, error);
    if (status.Failed())
    {
        UNSCOPED_INFO("ImportGltf: " << GltfMessage(*error, status));
    }
    return status;
}

usize CountOfType(const ImportResult& result, ResourceType type)
{
    usize n = 0;
    for (const ImportedResource& r : result.resources)
    {
        n += r.type == type ? 1 : 0;
    }
    return n;
}

const ImportedResource* FirstOfType(const ImportResult& result, ResourceType type)
{
    for (const ImportedResource& r : result.resources)
    {
        if (r.type == type)
        {
            return &r;
        }
    }
    return nullptr;
}

bool Warned(const ImportResult& result, const char* text)
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

// The level of an import, loaded into a world, with its entities found by name.
struct LoadedLevel
{
    TypeRegistry types;
    storage::ComponentRegistry registry;
    ecs::World world;
    storage::KeyIndex index;

    explicit LoadedLevel(const ImportResult& result)
    {
        REQUIRE(storage::RegisterBuiltinComponents(&registry).Ok());
        REQUIRE(RegisterLevelComponents(&registry).Ok());
        REQUIRE(animation::RegisterAnimationComponents(&registry).Ok());
        REQUIRE(scene::RegisterSceneComponents(&registry).Ok());
        const ImportedResource* level = FirstOfType(result, types::kLevel);
        REQUIRE(level != nullptr);
        REQUIRE(LoadLevelBlob(ByteSpan(level->blob.Data(), level->blob.Size()), types, registry, world, index).Ok());
    }

    ecs::Entity Find(const char* name)
    {
        ecs::Entity found;
        world.Query<ecs::Read<EntityName>>().ForEach(
            [&](ecs::Entity e, const EntityName& n)
            {
                if (n.name.View() == StringView(name))
                {
                    found = e;
                }
            });
        return found;
    }
};

}  // namespace

TEST_CASE("gltf collider: a -col node is drawn and is a collider", "[asset][gltf][collision]")
{
    ImportResult result;
    GltfError error;
    ImportOptions options;
    options.scale = 2.0f;
    REQUIRE(Import(CollisionGlb("\"nodes\":[{\"name\":\"ground-col\",\"mesh\":0}]", 1), &result, &error, options).Ok());
    CHECK(CountOfType(result, types::kMesh) == 1);
    REQUIRE(CountOfType(result, types::kMeshCollider) == 1);
    const ImportedResource* mcol = FirstOfType(result, types::kMeshCollider);
    CHECK(mcol->path.View() == StringView("/gltf/colliders/slab"));
    MeshColliderView view;
    REQUIRE(view.Open(CopyShared(ByteSpan(mcol->blob.Data(), mcol->blob.Size())), TypeRegistry()).Ok());
    CHECK(view.TriangleCount() == 2);
    CHECK(view.Position(1)[0] == 2.0f);  // the import's scale applied
    LoadedLevel level(result);
    const ecs::Entity ground = level.Find("ground-col");
    REQUIRE(ground.IsValid());
    CHECK(level.world.Has<MeshReference>(ground));
    const StaticCollider* collider = level.world.Peek<StaticCollider>(ground);
    REQUIRE(collider != nullptr);
    CHECK(collider->shape == StaticColliderShape::Mesh);
    CHECK(collider->mesh.id == mcol->id.Value());
    CHECK(result.summary.View().Contains(
        StringView("\"collision\":{\"colliders\":1,\"meshes\":1,\"triangles\":2,\"primitives\":0,\"dropped\":0}")));
}

TEST_CASE("gltf collider: a -colonly node is a collider only, and its mesh is not made", "[asset][gltf][collision]")
{
    ImportResult result;
    GltfError error;
    REQUIRE(Import(CollisionGlb("\"nodes\":[{\"name\":\"wall-colonly\",\"mesh\":0}]", 1), &result, &error).Ok());
    CHECK(CountOfType(result, types::kMesh) == 0);
    CHECK(CountOfType(result, types::kMeshCollider) == 1);
    LoadedLevel level(result);
    const ecs::Entity wall = level.Find("wall-colonly");
    REQUIRE(wall.IsValid());
    CHECK(!level.world.Has<MeshReference>(wall));
    CHECK(level.world.Has<StaticCollider>(wall));
    // The mesh is still made when another node draws it.
    ImportResult both;
    REQUIRE(
        Import(CollisionGlb("\"nodes\":[{\"name\":\"wall-colonly\",\"mesh\":0},{\"name\":\"shown\",\"mesh\":0}]", 2),
               &both, &error)
            .Ok());
    CHECK(CountOfType(both, types::kMesh) == 1);
}

TEST_CASE("gltf collider: the suffix rules", "[asset][gltf][collision]")
{
    ImportResult result;
    GltfError error;
    REQUIRE(Import(CollisionGlb("\"nodes\":[{\"name\":\"a-col.001\",\"mesh\":0},{\"name\":\"b-col\",\"mesh\":0},"
                                "{\"name\":\"c-colx\",\"mesh\":0},{\"name\":\"d-col-e\",\"mesh\":0},"
                                "{\"name\":\"f-convcol\",\"mesh\":0},{\"name\":\"g-COL\",\"mesh\":0}]",
                                6),
                   &result, &error)
                .Ok());
    CHECK(CountOfType(result, types::kMeshCollider) == 1);  // one MCOL, shared by every node that uses the mesh
    LoadedLevel level(result);
    CHECK(level.world.Has<StaticCollider>(level.Find("a-col.001")));  // Blender's duplicate number is set aside
    CHECK(level.world.Has<StaticCollider>(level.Find("b-col")));
    CHECK(!level.world.Has<StaticCollider>(level.Find("c-colx")));    // not at the end
    CHECK(!level.world.Has<StaticCollider>(level.Find("d-col-e")));   // in the middle
    CHECK(level.world.Has<StaticCollider>(level.Find("f-convcol")));  // convex: a triangle mesh, with a warning
    CHECK(!level.world.Has<StaticCollider>(level.Find("g-COL")));     // case-sensitive
    CHECK(Warned(result, "convex"));
    CHECK(result.summary.View().Contains(StringView("\"colliders\":3")));
}

TEST_CASE("gltf collider: triangles with no area are dropped and counted", "[asset][gltf][collision]")
{
    ImportResult result;
    GltfError error;
    REQUIRE(Import(CollisionGlb("\"nodes\":[{\"name\":\"ground-col\",\"mesh\":0}]", 1, true), &result, &error).Ok());
    const ImportedResource* mcol = FirstOfType(result, types::kMeshCollider);
    REQUIRE(mcol != nullptr);
    MeshColliderView view;
    REQUIRE(view.Open(CopyShared(ByteSpan(mcol->blob.Data(), mcol->blob.Size())), TypeRegistry()).Ok());
    CHECK(view.TriangleCount() == 2);
    CHECK(Warned(result, "1 triangles with no area are left out"));
}

TEST_CASE("gltf collider: a mesh with no triangle left, or a node without a mesh, gives no collider",
          "[asset][gltf][collision]")
{
    // Every triangle degenerate: positions all at one point.
    GltfWriter w;
    const f32 flat[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
    w.AddAccessor(w.AddView(flat, sizeof(flat)), 5126, 3, "VEC3", ",\"min\":[1,1,1],\"max\":[1,1,1]");
    w.AddRoot("\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
              "\"nodes\":[{\"name\":\"dust-colonly\",\"mesh\":0},{\"name\":\"empty-col\"}],"
              "\"scenes\":[{\"nodes\":[0,1]}],\"scene\":0");
    ImportResult result;
    GltfError error;
    REQUIRE(Import(w.Glb(), &result, &error).Ok());
    CHECK(CountOfType(result, types::kMeshCollider) == 0);
    CHECK(Warned(result, "no triangle with an area"));
    CHECK(Warned(result, "has no mesh"));
    CHECK(result.summary.View().Contains(StringView("\"colliders\":0")));
    CHECK(result.summary.View().Contains(StringView("\"dropped\":2")));
    LoadedLevel level(result);
    CHECK(!level.world.Has<StaticCollider>(level.Find("dust-colonly")));
}

TEST_CASE("gltf collider: hostile geometry is refused naming it", "[asset][gltf][collision]")
{
    ImportResult result;
    GltfError error;
    {  // a non-finite position (the accessor reader refuses it)
        GltfWriter w;
        const f32 bad[9] = {0, 0, 0, 1, 0, 0, 0, Limits<f32>::QuietNaN(), 0};
        w.AddAccessor(w.AddView(bad, sizeof(bad)), 5126, 3, "VEC3");
        w.AddRoot("\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
                  "\"nodes\":[{\"name\":\"x-colonly\",\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0");
        CHECK(Import(w.Glb(), &result, &error).Is(ErrorCode::Invalid));
    }
    {  // an index outside the vertices
        GltfWriter w;
        const u16 indices[3] = {0, 1, 7};
        w.AddAccessor(w.AddView(kQuad, 36), 5126, 3, "VEC3", ",\"min\":[-1,0,-1],\"max\":[1,0,1]");
        w.AddAccessor(w.AddView(indices, sizeof(indices)), 5123, 3, "SCALAR");
        w.AddRoot("\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
                  "\"nodes\":[{\"name\":\"x-colonly\",\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0");
        CHECK(Import(w.Glb(), &result, &error).Is(ErrorCode::Invalid));
        CHECK(StringView(error.message.Data(), CStrLength(error.message.Data())).Contains(StringView("index")));
    }
}

TEST_CASE("gltf collider: what cannot be honoured warns", "[asset][gltf][collision]")
{
    GltfWriter w;
    const u16 indices[6] = {0, 2, 1, 0, 3, 2};
    w.AddAccessor(w.AddView(kQuad, 48), 5126, 4, "VEC3", ",\"min\":[-1,0,-1],\"max\":[1,0,1]");
    w.AddAccessor(w.AddView(indices, sizeof(indices)), 5123, 6, "SCALAR");
    // A LINES primitive beside a TRIANGLES one; morph targets on the mesh; a parent with a non-uniform scale and a
    // rotated child (a shear in the child's world transform).
    w.AddRoot(
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"targets\":[{\"POSITION\":0}]},"
        "{\"attributes\":{\"POSITION\":0},\"mode\":1}],\"weights\":[0]}],"
        "\"nodes\":[{\"name\":\"stretch\",\"scale\":[1,3,1],\"children\":[1]},"
        "{\"name\":\"tilted-colonly\",\"mesh\":0,\"rotation\":[0,0,0.38268343,0.92387953]}],"
        "\"scenes\":[{\"nodes\":[0]}],\"scene\":0");
    ImportResult result;
    GltfError error;
    REQUIRE(Import(w.Glb(), &result, &error).Ok());
    CHECK(Warned(result, "is not TRIANGLES"));
    CHECK(Warned(result, "morph targets"));
    CHECK(Warned(result, "shear"));
    CHECK(CountOfType(result, types::kMeshCollider) == 1);
}

TEST_CASE("gltf collider: the collision triangle limit refuses an import over it", "[asset][gltf][collision]")
{
    ImportOptions options;
    options.limits.max_collision_triangles = 1;
    ImportResult result;
    GltfError error;
    CHECK(Import(CollisionGlb("\"nodes\":[{\"name\":\"ground-col\",\"mesh\":0}]", 1), &result, &error, options)
              .Is(ErrorCode::TooLarge));
    CHECK(StringView(error.message.Data(), CStrLength(error.message.Data()))
              .Contains(StringView("max_collision_triangles")));
    // The setting is the limit's default.
    settings::Registry registry;
    RegisterImportSettings(&registry);
    CHECK(ImportLimitsFromSettings(registry).max_collision_triangles == ImportLimits().max_collision_triangles);
    CHECK(ImportLimits().max_collision_triangles == 4194304u);
}

TEST_CASE("gltf collider: a marked node outside the default scene makes nothing", "[asset][gltf][collision]")
{
    ImportResult result;
    GltfError error;
    // Node 1 is not in the scene (roots: node 0 only).
    REQUIRE(Import(CollisionGlb("\"nodes\":[{\"name\":\"ground\",\"mesh\":0},{\"name\":\"stray-col\",\"mesh\":0}]", 1),
                   &result, &error)
                .Ok());
    CHECK(CountOfType(result, types::kMeshCollider) == 0);
    CHECK(result.summary.View().Contains(StringView("\"colliders\":0")));
}

TEST_CASE("gltf collider: a file without marks makes no collider", "[asset][gltf][collision]")
{
    ImportResult result;
    GltfError error;
    REQUIRE(Import(CollisionGlb("\"nodes\":[{\"name\":\"ground\",\"mesh\":0}]", 1), &result, &error).Ok());
    CHECK(CountOfType(result, types::kMeshCollider) == 0);
    CHECK(CountOfType(result, types::kMesh) == 1);
    CHECK(result.summary.View().Contains(
        StringView("\"collision\":{\"colliders\":0,\"meshes\":0,\"triangles\":0,\"primitives\":0,\"dropped\":0}")));
    LoadedLevel level(result);
    CHECK(!level.world.Has<StaticCollider>(level.Find("ground")));
    // Without a level, a marked file makes no collider and keeps every mesh.
    ImportResult plain;
    NoGltfSource none;
    SeededIdSource ids(11);
    ImportOptions no_level;
    no_level.ids = &ids;
    const Vector<byte> glb = CollisionGlb("\"nodes\":[{\"name\":\"wall-colonly\",\"mesh\":0}]", 1);
    REQUIRE(ImportGltf(ByteSpan(glb.Data(), glb.Size()), none, no_level, &plain, &error).Ok());
    CHECK(CountOfType(plain, types::kMeshCollider) == 0);
    CHECK(CountOfType(plain, types::kMesh) == 1);
}

TEST_CASE("gltf collider: two imports give the same MCOL bytes and ids", "[asset][gltf][collision]")
{
    const Vector<byte> glb = CollisionGlb("\"nodes\":[{\"name\":\"ground-col\",\"mesh\":0}]", 1, true);
    ImportResult one;
    ImportResult two;
    GltfError error;
    REQUIRE(Import(glb, &one, &error).Ok());
    REQUIRE(Import(glb, &two, &error).Ok());
    const ImportedResource* a = FirstOfType(one, types::kMeshCollider);
    const ImportedResource* b = FirstOfType(two, types::kMeshCollider);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a->id == b->id);
    REQUIRE(a->blob.Size() == b->blob.Size());
    CHECK(MemCompare(a->blob.Data(), b->blob.Data(), a->blob.Size()) == 0);
    // Through the manifest, with another id source: the MCOL keeps its id.
    String<HeapAllocator> text(HeapAllocator::Default());
    one.manifest.Format(&text);
    ImportManifest manifest;
    REQUIRE(manifest.Parse(text.View(), &error).Ok());
    ImportOptions again;
    SeededIdSource other(999);
    again.ids = &other;
    again.manifest = &manifest;
    ImportResult three;
    REQUIRE(Import(glb, &three, &error, again).Ok());
    CHECK(FirstOfType(three, types::kMeshCollider)->id == a->id);
}

// glTF colliders Day 4: OMI_physics_body and OMI_physics_shape.
namespace
{

Vector<byte> OmiGlb(const char* shapes_json, const char* nodes_json, u32 roots, const char* body_root = "")
{
    char extra[2048];
    FormatTo(extra, sizeof(extra),
             ",\"extensionsUsed\":[\"OMI_physics_body\",\"OMI_physics_shape\"],"
             "\"extensionsRequired\":[\"OMI_physics_body\",\"OMI_physics_shape\"],"
             "\"extensions\":{\"OMI_physics_shape\":{\"shapes\":[%s]}%s}",
             shapes_json, body_root);
    return CollisionGlb(nodes_json, roots, false, "slab", extra);
}

}  // namespace

TEST_CASE("gltf collider: OMI boxes, spheres, capsules and triangle meshes", "[asset][gltf][collision][omi]")
{
    ImportResult result;
    GltfError error;
    REQUIRE(
        Import(
            OmiGlb("{\"type\":\"box\",\"box\":{\"size\":[2,4,6]}},{\"type\":\"sphere\",\"sphere\":{\"radius\":0.25}},"
                   "{\"type\":\"capsule\",\"capsule\":{\"height\":1.5,\"radiusTop\":0.3,\"radiusBottom\":0.3}},"
                   "{\"type\":\"trimesh\",\"trimesh\":{\"mesh\":0}},{\"type\":\"sphere\"}",
                   "\"nodes\":[{\"name\":\"crate\",\"extensions\":{\"OMI_physics_body\":{\"collider\":{\"shape\":0}}}},"
                   "{\"name\":\"ball\",\"extensions\":{\"OMI_physics_body\":{\"collider\":{\"shape\":1}}}},"
                   "{\"name\":\"post\",\"extensions\":{\"OMI_physics_body\":{\"collider\":{\"shape\":2}}}},"
                   "{\"name\":\"floor\",\"mesh\":0,\"extensions\":{\"OMI_physics_body\":{\"collider\":{\"shape\":3}}}},"
                   "{\"name\":\"plain\",\"extensions\":{\"OMI_physics_body\":{\"collider\":{\"shape\":4}}}}]",
                   5),
            &result, &error)
            .Ok());
    LoadedLevel level(result);
    const StaticCollider* box = level.world.Peek<StaticCollider>(level.Find("crate"));
    REQUIRE(box != nullptr);
    CHECK(box->shape == StaticColliderShape::Box);
    CHECK(box->half_extents.x == 1.0f);
    CHECK(box->half_extents.z == 3.0f);
    const StaticCollider* ball = level.world.Peek<StaticCollider>(level.Find("ball"));
    REQUIRE(ball != nullptr);
    CHECK(ball->shape == StaticColliderShape::Sphere);
    CHECK(ball->radius == 0.25f);
    const StaticCollider* post = level.world.Peek<StaticCollider>(level.Find("post"));
    REQUIRE(post != nullptr);
    CHECK(post->shape == StaticColliderShape::Capsule);
    CHECK(post->radius == 0.3f);
    CHECK(post->half_height == 0.75f);  // the specification's height is between the caps' centres
    const StaticCollider* floor = level.world.Peek<StaticCollider>(level.Find("floor"));
    REQUIRE(floor != nullptr);
    CHECK(floor->shape == StaticColliderShape::Mesh);
    CHECK(level.world.Has<MeshReference>(level.Find("floor")));  // an OMI collider keeps the node's mesh drawn
    const StaticCollider* plain = level.world.Peek<StaticCollider>(level.Find("plain"));
    REQUIRE(plain != nullptr);
    CHECK(plain->radius == 0.5f);  // the specification's default
    CHECK(result.summary.View().Contains(StringView("\"colliders\":5")));
    CHECK(result.summary.View().Contains(StringView("\"primitives\":4")));
}

TEST_CASE("gltf collider: OMI shapes and bodies the engine does not import warn", "[asset][gltf][collision][omi]")
{
    ImportResult result;
    GltfError error;
    REQUIRE(
        Import(
            OmiGlb(
                "{\"type\":\"cylinder\"},{\"type\":\"convex\",\"convex\":{\"mesh\":0}},{\"type\":\"box\"}",
                "\"nodes\":[{\"name\":\"can\",\"extensions\":{\"OMI_physics_body\":{\"collider\":{\"shape\":0}}}},"
                "{\"name\":\"rock\",\"extensions\":{\"OMI_physics_body\":{\"collider\":{\"shape\":1}}}},"
                "{\"name\":\"prop\",\"extensions\":{\"OMI_physics_body\":{\"motion\":{\"type\":\"dynamic\"},"
                "\"collider\":{\"shape\":2}}}},"
                "{\"name\":\"zone\",\"extensions\":{\"OMI_physics_body\":{\"trigger\":{\"shape\":2}}}},"
                "{\"name\":\"both-col\",\"mesh\":0,\"extensions\":{\"OMI_physics_body\":{\"collider\":{\"shape\":2}}}}]",
                5),
            &result, &error)
            .Ok());
    CHECK(Warned(result, "no collider for"));       // the cylinder
    CHECK(Warned(result, "convex"));                // the convex shape is a triangle mesh
    CHECK(Warned(result, "imported as a static"));  // the dynamic body
    CHECK(Warned(result, "triggers are not imported"));
    CHECK(Warned(result, "the OMI one is used"));  // a collider name and an OMI collider
    LoadedLevel level(result);
    CHECK(!level.world.Has<StaticCollider>(level.Find("can")));
    CHECK(level.world.Peek<StaticCollider>(level.Find("rock"))->shape == StaticColliderShape::Mesh);
    CHECK(level.world.Has<StaticCollider>(level.Find("prop")));
    CHECK(!level.world.Has<StaticCollider>(level.Find("zone")));
    CHECK(level.world.Peek<StaticCollider>(level.Find("both-col"))->shape == StaticColliderShape::Box);
}

TEST_CASE("gltf collider: OMI physics materials give friction and restitution", "[asset][gltf][collision][omi]")
{
    ImportResult result;
    GltfError error;
    REQUIRE(
        Import(OmiGlb("{\"type\":\"box\"}",
                      "\"nodes\":[{\"name\":\"ice\",\"extensions\":{\"OMI_physics_body\":{\"collider\":{\"shape\":0,"
                      "\"physicsMaterial\":0}}}},"
                      "{\"name\":\"odd\",\"extensions\":{\"OMI_physics_body\":{\"collider\":{\"shape\":0,"
                      "\"physicsMaterial\":1}}}}]",
                      2,
                      ",\"OMI_physics_body\":{\"physicsMaterials\":[{\"dynamicFriction\":0.05,\"restitution\":0.2},"
                      "{\"dynamicFriction\":-3,\"restitution\":4}]}"),
               &result, &error)
            .Ok());
    LoadedLevel level(result);
    const StaticCollider* ice = level.world.Peek<StaticCollider>(level.Find("ice"));
    REQUIRE(ice != nullptr);
    CHECK(ice->friction == 0.05f);
    CHECK(ice->restitution == 0.2f);
    const StaticCollider* odd = level.world.Peek<StaticCollider>(level.Find("odd"));
    REQUIRE(odd != nullptr);
    CHECK(odd->friction == 0.0f);
    CHECK(odd->restitution == 1.0f);
    CHECK(Warned(result, "clamped"));
}

TEST_CASE("gltf collider: hostile OMI collision is refused naming it", "[asset][gltf][collision][omi]")
{
    ImportResult result;
    GltfError error;
    const auto refused = [&](const char* shapes, const char* collider, const char* path_part)
    {
        char nodes[512];
        FormatTo(nodes, sizeof(nodes),
                 "\"nodes\":[{\"name\":\"x\",\"extensions\":{\"OMI_physics_body\":{\"collider\":%s}}}]", collider);
        const Status s = Import(OmiGlb(shapes, nodes, 1), &result, &error);
        CHECK(s.Failed());
        CHECK(StringView(error.message.Data(), CStrLength(error.message.Data())).Contains(StringView(path_part)));
    };
    refused("{\"type\":\"box\"}", "{\"shape\":7}", "shape");                             // index out of range
    refused("{\"type\":\"box\"}", "{\"shape\":-1}", "shape");                            // the "none" default
    refused("{\"type\":\"box\",\"box\":{\"size\":[1,-2,1]}}", "{\"shape\":0}", "size");  // a negative size
    refused("{\"type\":\"sphere\",\"sphere\":{\"radius\":1e30}}", "{\"shape\":0}", "radius");
    refused("{\"type\":\"trimesh\",\"trimesh\":{\"mesh\":9}}", "{\"shape\":0}", "mesh");  // mesh out of range
    refused("{\"type\":\"box\"}", "{\"shape\":0,\"physicsMaterial\":3}", "physicsMaterial");
    // A collider but no shapes array at all.
    const Vector<byte> no_shapes = CollisionGlb(
        "\"nodes\":[{\"name\":\"x\",\"extensions\":{\"OMI_physics_body\":{\"collider\":{\"shape\":0}}}}]", 1, false,
        "slab", ",\"extensionsUsed\":[\"OMI_physics_body\"]");
    CHECK(Import(no_shapes, &result, &error).Failed());
}

}  // namespace engine_placeholder::asset
