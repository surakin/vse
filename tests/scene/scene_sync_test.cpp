#include <catch2/catch_test_macros.hpp>

#include "containers/shared_ptr.h"
#include "containers/vector.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "renderer/mesh.h"
#include "renderer/render_scene.h"
#include "rhi/null/null_device.h"
#include "scene/render_sync.h"

using namespace engine_placeholder;
using scene::ActiveCamera;
using scene::CameraOutput;
using scene::Hidden;
using scene::LightSource;
using scene::MakeCameraSystem;
using scene::MakeRenderSyncSystem;
using scene::MeshAsset;
using scene::MeshRenderer;
using scene::RenderHandle;
using scene::SceneLink;
using scene::SceneSyncReport;

namespace
{

ecs::LocalTransform At(f32 x, f32 y, f32 z)
{
    ecs::LocalTransform t;
    t.position = Vec3(x, y, z);
    return t;
}

// A world, a null device, a render scene and the frame: transforms, then the camera and the render sync in the render
// stage.
struct Rig
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    renderer::RenderScene render_scene;
    ecs::World world;
    ecs::Schedule schedule;
    SharedPtr<MeshAsset> cube;
    SharedPtr<MeshAsset> sphere;

    explicit Rig(u32 max_objects = 1024, u32 max_lights = 64) : render_scene(MakeDesc(max_objects, max_lights))
    {
        renderer::MeshData data;
        REQUIRE(renderer::MakeCubeMesh(1.0f, &data) == nullptr);
        cube = MakeShared<MeshAsset>();
        REQUIRE(cube->mesh.Create(device.Get(), data, "cube"));
        renderer::MeshData round;
        REQUIRE(renderer::MakeSphereMesh(1.0f, 8, 4, &round) == nullptr);
        sphere = MakeShared<MeshAsset>();
        REQUIRE(sphere->mesh.Create(device.Get(), round, "sphere"));
        world.SetResource(ecs::TransformReport{});
        world.SetResource(SceneLink{&render_scene});
        world.SetResource(SceneSyncReport{});
        world.SetResource(ActiveCamera{});
        world.SetResource(CameraOutput{});
        schedule.Add(ecs::MakeTransformSystem("transforms"));
        schedule.Add(MakeCameraSystem("camera").InStage(ecs::kRender));
        schedule.Add(MakeRenderSyncSystem("render_sync").InStage(ecs::kRender));
        REQUIRE(schedule.Build().ok);
    }
    static renderer::RenderSceneDesc MakeDesc(u32 objects, u32 lights)
    {
        renderer::RenderSceneDesc d;
        d.max_objects = objects;
        d.max_lights = lights;
        return d;
    }
    void Frame() { schedule.Run(world, 0.016f); }
    const SceneSyncReport& Report() { return world.Resource<SceneSyncReport>(); }
    ecs::Entity Spawn(f32 x, f32 y, f32 z, const SharedPtr<MeshAsset>& mesh, u32 material = 0)
    {
        MeshRenderer mr;
        mr.mesh = mesh;
        mr.material = material;
        return world.Create(At(x, y, z), ecs::WorldTransform{}, mr);
    }
    // The object an entity is mirrored as (null when it has none).
    const renderer::RenderObject* ObjectOf(ecs::Entity e)
    {
        const RenderHandle* handle = world.Get<RenderHandle>(e);
        return handle == nullptr ? nullptr : render_scene.FindObject(handle->object);
    }
};

}  // namespace

TEST_CASE("Scene sync: N entities add N objects with the entities' matrices", "[scene]")
{
    Rig rig;
    Vector<ecs::Entity> ids;
    for (u32 i = 0; i < 50; ++i)
    {
        ids.Push(rig.Spawn(static_cast<f32>(i), 2.0f, 3.0f, rig.cube, i));
    }
    rig.Frame();
    rig.Frame();
    CHECK(rig.render_scene.ObjectCount() == 50);
    CHECK(rig.Report().objects_added == 50);
    for (u32 i = 0; i < 50; ++i)
    {
        const renderer::RenderObject* object = rig.ObjectOf(ids[i]);
        REQUIRE(object != nullptr);
        CHECK(object->transform.col[3].x == static_cast<f32>(i));
        CHECK(object->transform.col[3].y == 2.0f);
        CHECK(object->material == i);
        CHECK(object->mesh == &rig.cube->mesh);
    }
}

TEST_CASE("Scene sync: moving a parent moves its subtree's objects, calling SetTransform only for the changed",
          "[scene]")
{
    Rig rig;
    const ecs::Entity parent = rig.Spawn(10, 0, 0, rig.cube);
    const ecs::Entity kid = rig.Spawn(1, 0, 0, rig.cube);
    const ecs::Entity grandkid = rig.Spawn(0, 1, 0, rig.cube);
    const ecs::Entity other = rig.Spawn(50, 0, 0, rig.cube);
    REQUIRE(ecs::SetParent(rig.world, kid, parent) == ecs::HierarchyStatus::Ok);
    REQUIRE(ecs::SetParent(rig.world, grandkid, kid) == ecs::HierarchyStatus::Ok);
    rig.Frame();
    rig.Frame();
    rig.Frame();
    CHECK(rig.ObjectOf(grandkid)->transform.col[3].x == 11.0f);
    const u64 sets = rig.Report().transforms_set;
    rig.world.Get<ecs::LocalTransform>(parent)->position = Vec3(20, 0, 0);
    rig.Frame();
    CHECK(rig.ObjectOf(parent)->transform.col[3].x == 20.0f);
    CHECK(rig.ObjectOf(kid)->transform.col[3].x == 21.0f);
    CHECK(rig.ObjectOf(grandkid)->transform.col[3].x == 21.0f);
    CHECK(rig.ObjectOf(other)->transform.col[3].x == 50.0f);
    CHECK(rig.Report().transforms_set - sets == 3);  // the subtree, not the other entity
}

TEST_CASE("Scene sync: destroying an entity removes its object and its light", "[scene]")
{
    Rig rig;
    const ecs::Entity mesh_entity = rig.Spawn(0, 0, 0, rig.cube);
    LightSource light;
    const ecs::Entity light_entity = rig.world.Create(At(1, 2, 3), ecs::WorldTransform{}, light);
    rig.Frame();
    rig.Frame();
    CHECK(rig.render_scene.ObjectCount() == 1);
    CHECK(rig.render_scene.LightCount() == 1);
    rig.world.Destroy(mesh_entity);
    rig.world.Destroy(light_entity);
    rig.Frame();
    CHECK(rig.render_scene.ObjectCount() == 0);
    CHECK(rig.render_scene.LightCount() == 0);
    CHECK(rig.Report().objects_removed == 1);
    CHECK(rig.Report().lights_removed == 1);
    rig.Frame();
    rig.Frame();
    CHECK(rig.Report().objects_removed == 1);  // the removal log is not replayed
}

TEST_CASE("Scene sync: an entity destroyed in the frame it was mirrored does not leak its object", "[scene]")
{
    Rig rig;
    const ecs::Entity e = rig.Spawn(0, 0, 0, rig.cube);
    // Another system of the same stage destroys the entity: its command applies after the sync's own handle command was
    // recorded.
    rig.schedule.~Schedule();
    new (&rig.schedule) ecs::Schedule();
    rig.schedule.Add(ecs::MakeTransformSystem("transforms"));
    rig.schedule.Add(MakeRenderSyncSystem("render_sync").InStage(ecs::kRender));
    rig.schedule.Add(ecs::MakeSystem("killer", [e](ecs::SystemContext& context) { context.Commands().Destroy(e); })
                         .InStage(ecs::kRender));
    REQUIRE(rig.schedule.Build().ok);
    rig.Frame();
    rig.Frame();
    rig.Frame();
    CHECK(rig.render_scene.ObjectCount() == 0);
}

TEST_CASE("Scene sync: removing the MeshRenderer removes the object", "[scene]")
{
    Rig rig;
    const ecs::Entity e = rig.Spawn(0, 0, 0, rig.cube);
    rig.Frame();
    rig.Frame();
    REQUIRE(rig.render_scene.ObjectCount() == 1);
    rig.world.Remove<MeshRenderer>(e);
    rig.Frame();
    rig.Frame();
    CHECK(rig.render_scene.ObjectCount() == 0);
    CHECK(rig.world.Exists(e));
    // Giving it a renderer again mirrors it again.
    MeshRenderer mr;
    mr.mesh = rig.cube;
    rig.world.Add<MeshRenderer>(e, mr);
    rig.Frame();
    rig.Frame();
    CHECK(rig.render_scene.ObjectCount() == 1);
}

TEST_CASE("Scene sync: swapping the mesh or material re-adds the object", "[scene]")
{
    Rig rig;
    const ecs::Entity e = rig.Spawn(4, 0, 0, rig.cube, 1);
    rig.Frame();
    rig.Frame();
    CHECK(rig.ObjectOf(e)->mesh == &rig.cube->mesh);
    rig.world.Get<MeshRenderer>(e)->mesh = rig.sphere;
    rig.Frame();
    REQUIRE(rig.ObjectOf(e) != nullptr);
    CHECK(rig.ObjectOf(e)->mesh == &rig.sphere->mesh);
    CHECK(rig.ObjectOf(e)->transform.col[3].x == 4.0f);
    CHECK(rig.render_scene.ObjectCount() == 1);
    CHECK(rig.Report().readded == 1);
    rig.world.Get<MeshRenderer>(e)->material = 9;
    rig.Frame();
    CHECK(rig.ObjectOf(e)->material == 9);
    CHECK(rig.Report().readded == 2);
}

TEST_CASE("Scene sync: the Hidden tag sets and clears the hidden flag", "[scene]")
{
    Rig rig;
    const ecs::Entity e = rig.Spawn(0, 0, 0, rig.cube);
    const ecs::Entity born_hidden = rig.world.Create(At(0, 0, 0), ecs::WorldTransform{}, MeshRenderer{rig.cube, 0, 0},
                                                     Hidden{});
    rig.Frame();
    rig.Frame();
    CHECK((rig.ObjectOf(e)->flags & renderer::ObjectFlags::kHidden) == 0);
    CHECK((rig.ObjectOf(born_hidden)->flags & renderer::ObjectFlags::kHidden) != 0);
    rig.world.Add<Hidden>(e, Hidden{});
    rig.Frame();
    CHECK((rig.ObjectOf(e)->flags & renderer::ObjectFlags::kHidden) != 0);
    rig.world.Remove<Hidden>(e);
    rig.Frame();
    CHECK((rig.ObjectOf(e)->flags & renderer::ObjectFlags::kHidden) == 0);
    rig.world.Remove<Hidden>(born_hidden);
    rig.Frame();
    CHECK((rig.ObjectOf(born_hidden)->flags & renderer::ObjectFlags::kHidden) == 0);
}

TEST_CASE("Scene sync: a full render scene is a counted refusal, retried when there is room", "[scene]")
{
    Rig rig(4, 2);
    Vector<ecs::Entity> ids;
    for (u32 i = 0; i < 6; ++i)
    {
        ids.Push(rig.Spawn(static_cast<f32>(i), 0, 0, rig.cube));
    }
    rig.Frame();
    rig.Frame();
    CHECK(rig.render_scene.ObjectCount() == 4);
    CHECK(rig.Report().refused_full >= 2);
    rig.world.Destroy(ids[0]);
    rig.Frame();
    rig.Frame();
    CHECK(rig.render_scene.ObjectCount() == 4);  // one of the refused two got the room
    LightSource light;
    for (u32 i = 0; i < 3; ++i)
    {
        rig.world.Create(At(0, 0, 0), ecs::WorldTransform{}, light);
    }
    rig.Frame();
    rig.Frame();
    CHECK(rig.render_scene.LightCount() == 2);
}

TEST_CASE("Scene sync: a static scene makes no render scene calls", "[scene]")
{
    Rig rig(131072, 64);
    for (u32 i = 0; i < 100000; ++i)
    {
        rig.Spawn(static_cast<f32>(i), 0, 0, rig.cube);
    }
    rig.Frame();
    rig.Frame();
    rig.Frame();
    REQUIRE(rig.render_scene.ObjectCount() == 100000);
    const SceneSyncReport before = rig.Report();
    for (u32 frame = 0; frame < 100; ++frame)
    {
        rig.Frame();
    }
    const SceneSyncReport& after = rig.Report();
    CHECK(after.objects_added == before.objects_added);
    CHECK(after.objects_removed == before.objects_removed);
    CHECK(after.transforms_set == before.transforms_set);
    CHECK(after.flags_set == before.flags_set);
    CHECK(after.readded == before.readded);
}

TEST_CASE("Scene sync: the mesh asset outlives its entity until the scene entry is gone", "[scene]")
{
    Rig rig;
    SharedPtr<MeshAsset> temporary = MakeShared<MeshAsset>();
    renderer::MeshData data;
    REQUIRE(renderer::MakeCubeMesh(2.0f, &data) == nullptr);
    REQUIRE(temporary->mesh.Create(rig.device.Get(), data, "temporary"));
    const MeshAsset* raw = temporary.Get();
    const ecs::Entity e = rig.Spawn(0, 0, 0, temporary);
    rig.Frame();
    rig.Frame();
    temporary.Reset();     // the test lets go
    rig.world.Destroy(e);  // the entity's reference goes with it
    CHECK(rig.render_scene.ObjectCount() == 1);
    CHECK(rig.render_scene.ObjectAt(0).mesh == &raw->mesh);
    CHECK(raw->mesh.Valid());  // still alive: the sync's reference
    rig.Frame();
    CHECK(rig.render_scene.ObjectCount() == 0);  // after the sync removed the entry, the asset is freed
}

TEST_CASE("Scene sync: lights follow their entity and an unusable light is counted", "[scene]")
{
    Rig rig;
    LightSource point;
    point.radius = 5.0f;
    const ecs::Entity e = rig.world.Create(At(1, 2, 3), ecs::WorldTransform{}, point);
    LightSource bad;
    bad.radius = 0.0f;  // a point light needs a positive radius
    rig.world.Create(At(0, 0, 0), ecs::WorldTransform{}, bad);
    rig.Frame();
    rig.Frame();
    REQUIRE(rig.render_scene.LightCount() == 1);
    CHECK(rig.Report().invalid >= 1);
    const renderer::Light* light = rig.render_scene.FindLight(rig.world.Get<RenderHandle>(e)->light);
    REQUIRE(light != nullptr);
    CHECK(light->position.x == 1.0f);
    rig.world.Get<ecs::LocalTransform>(e)->position = Vec3(7, 0, 0);
    rig.Frame();
    CHECK(rig.render_scene.FindLight(rig.world.Get<RenderHandle>(e)->light)->position.x == 7.0f);
    rig.world.Get<LightSource>(e)->intensity = 3.0f;
    rig.Frame();
    CHECK(rig.render_scene.FindLight(rig.world.Get<RenderHandle>(e)->light)->intensity == 3.0f);
}

TEST_CASE("Scene sync: a directional light's direction follows the entity's rotation", "[scene]")
{
    Rig rig;
    LightSource sun;
    sun.type = renderer::LightType::Directional;
    sun.direction = Vec3(0, 1, 0);
    ecs::LocalTransform place = At(0, 0, 0);
    place.rotation = Quaternion::FromAxisAngle(Vec3(0, 0, 1), 1.5707964f);  // +Y turns to -X
    const ecs::Entity e = rig.world.Create(place, ecs::WorldTransform{}, sun);
    rig.Frame();
    rig.Frame();
    const renderer::Light* light = rig.render_scene.FindLight(rig.world.Get<RenderHandle>(e)->light);
    REQUIRE(light != nullptr);
    CHECK(light->direction.x < -0.999f);
}

TEST_CASE("Scene sync: the camera system builds the renderer camera from the active entity", "[scene]")
{
    Rig rig;
    scene::Camera component;
    component.fov_y = 1.0f;
    component.aspect = 2.0f;
    ecs::LocalTransform place = At(0, 0, 5);
    const ecs::Entity cam = rig.world.Create(place, ecs::WorldTransform{}, component);
    rig.world.Resource<ActiveCamera>().entity = cam;
    rig.Frame();
    const CameraOutput& out = rig.world.Resource<CameraOutput>();
    REQUIRE(out.valid);
    CHECK(out.camera.position.z == 5.0f);
    CHECK(out.camera.fov_y == 1.0f);
    CHECK(out.camera.aspect == 2.0f);
    const Vec3 forward = out.camera.Forward();
    CHECK(forward.z < -0.999f);  // looks down -Z
    rig.world.Destroy(cam);
    rig.Frame();
    CHECK_FALSE(rig.world.Resource<CameraOutput>().valid);
}

TEST_CASE("Scene sync: with no scene linked the sync does nothing", "[scene]")
{
    Rig rig;
    rig.Spawn(0, 0, 0, rig.cube);
    rig.world.Resource<SceneLink>().scene = nullptr;
    rig.Frame();
    CHECK(rig.Report().no_scene == 1);
    CHECK(rig.render_scene.ObjectCount() == 0);
}

TEST_CASE("Scene sync: a world with fresh removal numbers still removes", "[scene]")
{
    // A second world numbers its removals from 1 again: the sync, kept by a new schedule on the new world, must not
    // skip them.
    Rig rig;
    const ecs::Entity first = rig.Spawn(0, 0, 0, rig.cube);
    rig.Frame();
    rig.Frame();
    rig.world.Destroy(first);
    rig.Frame();
    REQUIRE(rig.render_scene.ObjectCount() == 0);
    ecs::World second;
    second.SetResource(ecs::TransformReport{});
    second.SetResource(SceneLink{&rig.render_scene});
    second.SetResource(SceneSyncReport{});
    second.SetResource(ActiveCamera{});
    second.SetResource(CameraOutput{});
    MeshRenderer mr;
    mr.mesh = rig.cube;
    const ecs::Entity e = second.Create(At(1, 0, 0), ecs::WorldTransform{}, mr);
    rig.schedule.Run(second, 0.016f);
    rig.schedule.Run(second, 0.016f);
    REQUIRE(rig.render_scene.ObjectCount() == 1);
    second.Destroy(e);
    rig.schedule.Run(second, 0.016f);
    CHECK(rig.render_scene.ObjectCount() == 0);
}
