// Terrains in the scene (Phase 28 Day 490): the sync system mirrors Terrain entities into the render scene, and the
// render scene's terrain list has fixed capacity and generation-checked ids. CPU only, on the null device.
#include <catch2/catch_test_macros.hpp>

#include "asset/gpu_loaders.h"
#include "asset/height_map.h"
#include "containers/shared_bytes.h"
#include "containers/shared_ptr.h"
#include "containers/vector.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "renderer/render_scene.h"
#include "rhi/null/null_device.h"
#include "scene/render_sync.h"

using namespace vse;
using scene::Hidden;
using scene::SceneLink;
using scene::SceneSyncReport;
using scene::Terrain;
using scene::TerrainHandle;

namespace
{

renderer::TerrainSourcePtr TsMakeSource(rhi::Device* device, u32 n, u16 fill)
{
    Vector<byte> samples;
    samples.Resize(static_cast<usize>(n) * n * 2);
    for (usize i = 0; i < static_cast<usize>(n) * n; ++i)
    {
        samples[i * 2] = static_cast<byte>(fill & 0xFF);
        samples[i * 2 + 1] = static_cast<byte>(fill >> 8);
    }
    asset::TypeRegistry types;
    asset::HeightMapHeader header;
    header.width = n;
    header.height = n;
    header.range_min = 0.0f;
    header.range_max = 10.0f;
    header.cell_size = 1.0f;
    Vector<byte> blob;
    REQUIRE(asset::BuildHeightMapBlob(types, asset::ResourceId::FromValue(5), header,
                                      ByteSpan(samples.Data(), samples.Size()), &blob)
                .Ok());
    asset::HeightMapView view;
    REQUIRE(view.Open(CopyShared(blob), types).Ok());
    renderer::TerrainSourcePtr source;
    REQUIRE(asset::MakeTerrainSource(device, view, nullptr, &source));
    return source;
}

struct TerrainRig
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    renderer::RenderScene render_scene;
    ecs::World world;
    ecs::Schedule schedule;

    explicit TerrainRig(u32 max_terrains = 8) : render_scene(Desc(max_terrains))
    {
        world.SetResource(ecs::TransformReport{});
        world.SetResource(SceneLink{&render_scene});
        world.SetResource(SceneSyncReport{});
        schedule.Add(ecs::MakeTransformSystem("transforms"));
        schedule.Add(scene::MakeTerrainSyncSystem("terrain_sync").InStage(ecs::kRender));
        REQUIRE(schedule.Build().ok);
    }
    static renderer::RenderSceneDesc Desc(u32 max_terrains)
    {
        renderer::RenderSceneDesc d;
        d.max_terrains = max_terrains;
        return d;
    }
    void Frame() { schedule.Run(world, 0.016f); }
    const SceneSyncReport& Report() { return world.Resource<SceneSyncReport>(); }
    ecs::Entity Spawn(f32 x, f32 z, const renderer::TerrainSourcePtr& source)
    {
        ecs::LocalTransform t;
        t.position = Vec3(x, 0.0f, z);
        Terrain terrain;
        terrain.source = source;
        return world.Create(t, ecs::WorldTransform{}, terrain);
    }
    const renderer::TerrainObject* ObjectOf(ecs::Entity e)
    {
        const TerrainHandle* handle = world.Get<TerrainHandle>(e);
        return handle == nullptr ? nullptr : render_scene.FindTerrain(handle->id);
    }
};

}  // namespace

TEST_CASE("Terrain sync: a terrain entity is mirrored with its placement and removed with the entity",
          "[scene][terrain]")
{
    TerrainRig rig;
    const renderer::TerrainSourcePtr source = TsMakeSource(rig.device.Get(), 65, 1000);
    const ecs::Entity e = rig.Spawn(100.0f, -50.0f, source);
    rig.Frame();
    rig.Frame();
    REQUIRE(rig.ObjectOf(e) != nullptr);
    CHECK(rig.render_scene.TerrainCount() == 1);
    CHECK(rig.ObjectOf(e)->world.col[3].x == 100.0f);
    CHECK(rig.ObjectOf(e)->world.col[3].z == -50.0f);
    CHECK(rig.ObjectOf(e)->source.Get() == source.Get());
    CHECK(rig.Report().terrains_added == 1);
    // the terrain object holds a share of the source: it outlives the entity's own
    rig.world.Destroy(e);
    rig.Frame();
    rig.Frame();
    CHECK(rig.render_scene.TerrainCount() == 0);
    CHECK(rig.Report().terrains_removed == 1);
}

TEST_CASE("Terrain sync: remove and re-add, hide, move, and a reloaded source", "[scene][terrain]")
{
    TerrainRig rig;
    const renderer::TerrainSourcePtr first = TsMakeSource(rig.device.Get(), 65, 1000);
    const renderer::TerrainSourcePtr second = TsMakeSource(rig.device.Get(), 65, 30000);
    const ecs::Entity e = rig.Spawn(0.0f, 0.0f, first);
    rig.Frame();
    rig.Frame();
    REQUIRE(rig.ObjectOf(e) != nullptr);
    // the component goes: the terrain goes, the handle too; it comes back with the component
    Terrain saved = *rig.world.Get<Terrain>(e);
    rig.world.Remove<Terrain>(e);
    rig.Frame();
    rig.Frame();
    CHECK(rig.render_scene.TerrainCount() == 0);
    CHECK(rig.world.Get<TerrainHandle>(e) == nullptr);
    rig.world.Add<Terrain>(e, saved);
    rig.Frame();
    rig.Frame();
    REQUIRE(rig.ObjectOf(e) != nullptr);
    CHECK(rig.render_scene.TerrainCount() == 1);
    // hot reload: a new source in the component is what the scene draws, the id stays
    const renderer::TerrainId id = rig.world.Get<TerrainHandle>(e)->id;
    const u64 sets = rig.Report().terrains_set;
    rig.world.Get<Terrain>(e)->source = second;
    rig.Frame();
    rig.Frame();
    CHECK(rig.ObjectOf(e)->source.Get() == second.Get());
    CHECK(rig.world.Get<TerrainHandle>(e)->id == id);
    CHECK(rig.Report().terrains_set == sets + 1);
    rig.Frame();  // nothing changed: nothing is set
    CHECK(rig.Report().terrains_set == sets + 1);
    // hidden
    rig.world.Add<Hidden>(e, Hidden{});
    rig.Frame();
    rig.Frame();
    CHECK((rig.ObjectOf(e)->flags & renderer::ObjectFlags::kHidden) != 0);
    rig.world.Remove<Hidden>(e);
    rig.Frame();
    rig.Frame();
    CHECK((rig.ObjectOf(e)->flags & renderer::ObjectFlags::kHidden) == 0);
    // moved
    rig.world.Get<ecs::LocalTransform>(e)->position = Vec3(5.0f, 0.0f, 6.0f);
    rig.Frame();
    rig.Frame();
    CHECK(rig.ObjectOf(e)->world.col[3].x == 5.0f);
}

TEST_CASE("Terrain sync: two terrains, capacity, and placements a terrain refuses", "[scene][terrain]")
{
    TerrainRig rig(2);
    const renderer::TerrainSourcePtr source = TsMakeSource(rig.device.Get(), 65, 1000);
    const ecs::Entity a = rig.Spawn(0.0f, 0.0f, source);
    const ecs::Entity b = rig.Spawn(64.0f, 0.0f, source);  // the neighbouring map, sharing the source
    rig.Frame();
    rig.Frame();
    CHECK(rig.render_scene.TerrainCount() == 2);
    CHECK(rig.ObjectOf(a) != nullptr);
    CHECK(rig.ObjectOf(b) != nullptr);
    // a third does not fit: counted, retried each frame, not drawn
    const ecs::Entity c = rig.Spawn(128.0f, 0.0f, source);
    rig.Frame();
    rig.Frame();
    CHECK(rig.ObjectOf(c) == nullptr);
    CHECK(rig.Report().refused_full >= 1);
    // room appears: it is added
    rig.world.Destroy(a);
    rig.Frame();
    rig.Frame();
    rig.Frame();
    CHECK(rig.ObjectOf(c) != nullptr);
    // a scaled terrain is invalid, not an abort
    const u64 invalid = rig.Report().invalid;
    ecs::LocalTransform scaled;
    scaled.scale = Vec3(2.0f, 2.0f, 2.0f);
    Terrain terrain;
    terrain.source = source;
    const ecs::Entity d = rig.world.Create(scaled, ecs::WorldTransform{}, terrain);
    rig.Frame();
    rig.Frame();
    CHECK(rig.ObjectOf(d) == nullptr);
    CHECK(rig.Report().invalid > invalid);
    // a terrain with no source is invalid too
    Terrain empty;
    const ecs::Entity f = rig.world.Create(ecs::LocalTransform{}, ecs::WorldTransform{}, empty);
    rig.Frame();
    CHECK(rig.ObjectOf(f) == nullptr);
}

TEST_CASE("Render scene: terrain ids are generation checked and a terrain is one object", "[scene][terrain]")
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    renderer::RenderSceneDesc desc;
    desc.max_objects = 1;
    desc.max_terrains = 1;
    renderer::RenderScene scene(desc);
    const renderer::TerrainSourcePtr source = TsMakeSource(device.Get(), 65, 0);
    renderer::TerrainObject object;
    object.source = source;
    const renderer::TerrainId id = scene.AddTerrain(object);
    REQUIRE(id.IsValid());
    CHECK(scene.ObjectCount() == 0);  // the terrain is not in the object list
    CHECK_FALSE(scene.AddTerrain(object).IsValid());
    scene.RemoveTerrain(id);
    CHECK(scene.FindTerrain(id) == nullptr);
    const renderer::TerrainId again = scene.AddTerrain(object);
    CHECK(again.index == id.index);
    CHECK(again != id);
    CHECK(scene.FindTerrain(id) == nullptr);
    CHECK(scene.FindTerrain(again) != nullptr);
    CHECK(renderer::TerrainWorldValid(Mat4::Identity()));
    Mat4 tilted = Mat4::Identity();
    tilted.col[1][2] = 0.5f;
    CHECK_FALSE(renderer::TerrainWorldValid(tilted));
}
