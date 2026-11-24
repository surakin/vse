// Instanced meshes in the scene: the sync system mirrors InstancedMesh entities into the render scene, the entity's
// transform is the set's parent, and AddMany and Scatter fill a set. CPU only, on the null device.
#include <catch2/catch_test_macros.hpp>

#include "containers/shared_ptr.h"
#include "containers/vector.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "renderer/instance_scatter.h"
#include "renderer/mesh.h"
#include "renderer/render_scene.h"
#include "rhi/null/null_device.h"
#include "scene/render_sync.h"

using namespace vse;
using scene::Hidden;
using scene::InstancedHandle;
using scene::InstancedMesh;
using scene::SceneLink;
using scene::SceneSyncReport;

namespace
{

renderer::GpuMeshPtr MakeMesh(rhi::Device* device)
{
    renderer::MeshData data;
    REQUIRE(renderer::MakeSphereMesh(1.0f, 8, 4, &data) == nullptr);
    renderer::GpuMeshPtr mesh = MakeShared<renderer::GpuMesh>();
    REQUIRE(mesh->Create(device, data, "sphere"));
    return mesh;
}

struct InstancedRig
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    renderer::RenderScene render_scene;
    ecs::World world;
    ecs::Schedule schedule;
    renderer::GpuMeshPtr mesh;

    explicit InstancedRig(u32 max_instanced = 2) : render_scene(renderer::RenderSceneDesc{16, 4, 2, max_instanced})
    {
        mesh = MakeMesh(device.Get());
        world.SetResource(ecs::TransformReport{});
        world.SetResource(SceneLink{&render_scene});
        world.SetResource(SceneSyncReport{});
        schedule.Add(ecs::MakeTransformSystem("transforms"));
        schedule.Add(scene::MakeInstancedSyncSystem("instanced_sync").InStage(ecs::kRender));
        REQUIRE(schedule.Build().ok);
    }
    void Frame() { schedule.Run(world, 0.016f); }
    const SceneSyncReport& Report() { return world.Resource<SceneSyncReport>(); }
    ecs::Entity Spawn(const Vec3& position, const renderer::InstanceSetPtr& set)
    {
        ecs::LocalTransform t;
        t.position = position;
        InstancedMesh component;
        component.mesh = mesh;
        component.set = set;
        return world.Create(t, ecs::WorldTransform{}, component);
    }
    const renderer::InstancedObject* ObjectOf(ecs::Entity e)
    {
        const InstancedHandle* handle = world.Get<InstancedHandle>(e);
        return handle == nullptr ? nullptr : render_scene.FindInstanced(handle->id);
    }
};

renderer::InstanceSetPtr MakeSetOf(u32 count, bool with_data = false)
{
    renderer::InstanceSetDesc d;
    d.has_data = with_data;
    renderer::InstanceSetPtr set = renderer::InstanceSet::Create(d);
    REQUIRE(set.Get() != nullptr);
    Vector<renderer::InstanceTransform> transforms;
    for (u32 i = 0; i < count; ++i)
    {
        renderer::InstanceTransform t;
        t.position = Vec3d(static_cast<f64>(i % 100), 0.0, static_cast<f64>(i / 100));
        transforms.Push(t);
    }
    const renderer::AddManyResult r = renderer::AddMany(
        set.Get(), Span<const renderer::InstanceTransform>(transforms.Data(), transforms.Size()), {});
    REQUIRE(r.status == renderer::InstanceStatus::Ok);
    REQUIRE(r.added == count);
    return set;
}

struct Filled
{
    renderer::InstanceSetPtr set;
    u32 added = 0;
};

bool RampHeight(void*, f64 x, f64 z, f64* height)
{
    if (x > 90.0)
    {
        return false;  // no surface past x = 90
    }
    *height = x * 0.5 + z * 0.25;
    return true;
}

}  // namespace

TEST_CASE("Instanced sync: an entity with 5,000 instances is mirrored and moves them all by moving",
          "[scene][instancing]")
{
    InstancedRig rig;
    const renderer::InstanceSetPtr set = MakeSetOf(5000);
    const ecs::Entity e = rig.Spawn(Vec3(10.0f, 0.0f, -5.0f), set);
    rig.Frame();
    rig.Frame();
    const renderer::InstancedObject* o = rig.ObjectOf(e);
    REQUIRE(o != nullptr);
    CHECK(rig.render_scene.InstancedCount() == 1);
    CHECK(o->set.Get() == set.Get());
    CHECK(o->position.x == 10.0f);
    CHECK(o->position.z == -5.0f);
    CHECK(o->scale == 1.0f);
    CHECK(rig.Report().instanced_added == 1);

    // Moving, turning and scaling the entity changes the scene entry, never the set.
    const u64 version = set->Version();
    rig.world.Get<ecs::LocalTransform>(e)->position = Vec3(0.0f, 3.0f, 0.0f);
    rig.world.Get<ecs::LocalTransform>(e)->scale = Vec3(2.0f, 2.0f, 2.0f);
    rig.Frame();
    rig.Frame();
    o = rig.ObjectOf(e);
    REQUIRE(o != nullptr);
    CHECK(o->position.y == 3.0f);
    CHECK(o->scale == 2.0f);
    CHECK(set->Version() == version);
    CHECK(rig.Report().instanced_set >= 1);
    CHECK(set->Count() == 5000);

    // Edits through the component's set show in the scene's (the same set).
    renderer::InstanceTransform extra;
    extra.position = Vec3d(1, 2, 3);
    renderer::InstanceId id;
    REQUIRE(rig.world.Get<InstancedMesh>(e)->set->Add(extra, nullptr, &id) == renderer::InstanceStatus::Ok);
    CHECK(rig.ObjectOf(e)->set->Count() == 5001);
    CHECK(rig.world.Get<InstancedMesh>(e)->set->Remove(id) == renderer::InstanceStatus::Ok);
    CHECK(rig.ObjectOf(e)->set->Count() == 5000);

    rig.world.Destroy(e);
    rig.Frame();
    rig.Frame();
    CHECK(rig.render_scene.InstancedCount() == 0);
    CHECK(rig.Report().instanced_removed == 1);
}

TEST_CASE("Instanced sync: hidden, a bad transform, a missing set, and the component coming and going",
          "[scene][instancing]")
{
    InstancedRig rig;
    const renderer::InstanceSetPtr set = MakeSetOf(10);
    const ecs::Entity e = rig.Spawn(Vec3(0, 0, 0), set);
    rig.Frame();
    rig.Frame();
    REQUIRE(rig.ObjectOf(e) != nullptr);
    CHECK((rig.ObjectOf(e)->flags & renderer::ObjectFlags::kHidden) == 0);

    rig.world.Add<Hidden>(e, Hidden{});
    rig.Frame();
    rig.Frame();
    REQUIRE(rig.ObjectOf(e) != nullptr);
    CHECK((rig.ObjectOf(e)->flags & renderer::ObjectFlags::kHidden) != 0);
    rig.world.Remove<Hidden>(e);
    rig.Frame();
    rig.Frame();
    CHECK((rig.ObjectOf(e)->flags & renderer::ObjectFlags::kHidden) == 0);

    // A non-uniform scale is not drawn and is counted.
    const u64 invalid = rig.Report().invalid;
    rig.world.Get<ecs::LocalTransform>(e)->scale = Vec3(1.0f, 2.0f, 1.0f);
    rig.Frame();
    rig.Frame();
    CHECK(rig.ObjectOf(e) == nullptr);
    CHECK(rig.render_scene.InstancedCount() == 0);
    CHECK(rig.Report().invalid > invalid);
    rig.world.Get<ecs::LocalTransform>(e)->scale = Vec3(1.0f, 1.0f, 1.0f);
    rig.Frame();
    rig.Frame();
    CHECK(rig.ObjectOf(e) != nullptr);

    // The component goes, the entry goes; it comes back with the component.
    InstancedMesh saved = *rig.world.Get<InstancedMesh>(e);
    rig.world.Remove<InstancedMesh>(e);
    rig.Frame();
    rig.Frame();
    CHECK(rig.render_scene.InstancedCount() == 0);
    CHECK(rig.world.Get<InstancedHandle>(e) == nullptr);
    rig.world.Add<InstancedMesh>(e, saved);
    rig.Frame();
    rig.Frame();
    CHECK(rig.ObjectOf(e) != nullptr);

    // No set: nothing is drawn.
    const ecs::Entity bare = rig.Spawn(Vec3(0, 0, 0), renderer::InstanceSetPtr());
    rig.Frame();
    CHECK(rig.ObjectOf(bare) == nullptr);
}

TEST_CASE("Instanced sync: the scene's capacity is a counted refusal, and the mesh outlives its entity",
          "[scene][instancing]")
{
    InstancedRig rig;
    const renderer::InstanceSetPtr set = MakeSetOf(4);
    const ecs::Entity a = rig.Spawn(Vec3(0, 0, 0), set);
    const ecs::Entity b = rig.Spawn(Vec3(1, 0, 0), set);
    const ecs::Entity c = rig.Spawn(Vec3(2, 0, 0), set);
    rig.Frame();
    rig.Frame();
    CHECK(rig.render_scene.InstancedCount() == 2);
    CHECK(rig.Report().refused_full > 0);
    static_cast<void>(a);
    static_cast<void>(b);
    static_cast<void>(c);

    // The mesh asset is shared: after the rig's own share goes, the sync's keeps it alive while the scene draws it.
    const renderer::GpuMesh* raw = rig.mesh.Get();
    rig.mesh.Reset();
    CHECK(raw->Valid());
    CHECK(rig.render_scene.InstancedCount() == 2);
}

TEST_CASE("AddMany adds a list, stops at the first refusal and checks its data list", "[scene][instancing]")
{
    renderer::InstanceSetDesc d;
    d.max_instances = 10;
    d.has_data = true;
    renderer::InstanceSetPtr set = renderer::InstanceSet::Create(d);
    REQUIRE(set.Get() != nullptr);
    Vector<renderer::InstanceTransform> transforms;
    Vector<renderer::InstanceExtra> data;
    for (u32 i = 0; i < 15; ++i)
    {
        renderer::InstanceTransform t;
        t.position = Vec3d(static_cast<f64>(i), 0, 0);
        transforms.Push(t);
        renderer::InstanceExtra e;
        e.custom = i;
        data.Push(e);
    }
    // A data list of another size adds nothing.
    renderer::AddManyResult bad = renderer::AddMany(set.Get(),
                                                    Span<const renderer::InstanceTransform>(transforms.Data(), 15),
                                                    Span<const renderer::InstanceExtra>(data.Data(), 3));
    CHECK(bad.status == renderer::InstanceStatus::InvalidValue);
    CHECK(bad.added == 0);
    CHECK(set->Count() == 0);
    // The set holds ten: ten are added, the rest refused as Full.
    const renderer::AddManyResult full = renderer::AddMany(
        set.Get(), Span<const renderer::InstanceTransform>(transforms.Data(), 15),
        Span<const renderer::InstanceExtra>(data.Data(), 15));
    CHECK(full.status == renderer::InstanceStatus::Full);
    CHECK(full.added == 10);
    CHECK(set->Count() == 10);
    // A bad value stops the list where it is.
    renderer::InstanceSetPtr second = renderer::InstanceSet::Create(renderer::InstanceSetDesc{});
    transforms[3].scale = Vec3(0.0f, 1.0f, 1.0f);
    const renderer::AddManyResult invalid = renderer::AddMany(
        second.Get(), Span<const renderer::InstanceTransform>(transforms.Data(), 15), {});
    CHECK(invalid.status == renderer::InstanceStatus::InvalidValue);
    CHECK(invalid.added == 3);
}

TEST_CASE("Scatter is deterministic, stays in its rectangle and follows a height field", "[scene][instancing]")
{
    renderer::ScatterRule rule;
    rule.seed = 42;
    rule.count = 2000;
    rule.min_x = 0.0;
    rule.max_x = 100.0;
    rule.min_z = 0.0;
    rule.max_z = 50.0;
    rule.min_scale = 0.5f;
    rule.max_scale = 1.5f;
    rule.lift = 0.25f;

    auto fill = [&](renderer::ScatterHeightFn fn)
    {
        renderer::InstanceSetPtr set = renderer::InstanceSet::Create(renderer::InstanceSetDesc{});
        const renderer::AddManyResult r = renderer::Scatter(set.Get(), rule, nullptr, fn, nullptr);
        REQUIRE(r.status == renderer::InstanceStatus::Ok);
        return Filled{set, r.added};
    };
    const auto a = fill(&RampHeight);
    const auto b = fill(&RampHeight);
    CHECK(a.added == b.added);
    CHECK(a.added < 2000);  // the points past x = 90 have no surface
    CHECK(a.added > 1500);
    CHECK(a.set->Count() == a.added);
    // The same seed gives the same instances (compare every chunk's records).
    REQUIRE(a.set->ChunkCount() == b.set->ChunkCount());
    for (u32 c = 0; c < a.set->ChunkCount(); ++c)
    {
        const renderer::InstanceChunkInfo ia = a.set->Chunk(c);
        const renderer::InstanceChunkInfo ib = b.set->Chunk(c);
        REQUIRE(ia.count == ib.count);
        for (u32 i = 0; i < ia.count; ++i)
        {
            const renderer::InstanceRecord& ra = a.set->Records(c)[i];
            const renderer::InstanceRecord& rb = b.set->Records(c)[i];
            CHECK(ra.position.x == rb.position.x);
            CHECK(ra.position.y == rb.position.y);
            CHECK(ra.rotation.w == rb.rotation.w);
            // On the surface plus the lift, inside the rectangle, with a scale in range.
            const f64 x = ia.origin.x + ra.position.x;
            const f64 y = ia.origin.y + ra.position.y;
            const f64 z = ia.origin.z + ra.position.z;
            CHECK(x >= 0.0);
            CHECK(x <= 90.0 + 1.0e-3);
            CHECK(z >= 0.0);
            CHECK(z <= 50.0);
            CHECK(Abs(y - (x * 0.5 + z * 0.25 + 0.25)) < 1.0e-3 * (1.0 + Abs(y)));
            CHECK(ra.scale.x >= 0.5f);
            CHECK(ra.scale.x <= 1.5f);
        }
    }
    // Another seed gives other instances; a flat scatter has one height.
    rule.seed = 43;
    const auto other = fill(&RampHeight);
    bool differs = other.added != a.added;
    if (!differs)
    {
        differs = other.set->Records(0)[0].position.x != a.set->Records(0)[0].position.x;
    }
    CHECK(differs);
    renderer::InstanceSetPtr flat = renderer::InstanceSet::Create(renderer::InstanceSetDesc{});
    rule.height = 7.0;
    rule.lift = 0.0f;
    const renderer::AddManyResult r = renderer::Scatter(flat.Get(), rule, nullptr, nullptr, nullptr);
    CHECK(r.added == 2000);
    renderer::InstanceTransform t;
    REQUIRE(flat->Get(flat->IdAt(0, 0), &t) == renderer::InstanceStatus::Ok);
    CHECK(t.position.y == 7.0);
    // A bad rule places nothing.
    rule.max_x = -1.0;
    CHECK(renderer::Scatter(flat.Get(), rule, nullptr, nullptr, nullptr).status ==
          renderer::InstanceStatus::InvalidValue);
}

TEST_CASE("Instanced sync: 100,000 entities created in one frame sync, and their removal releases the meshes",
          "[scene][instancing]")
{
    // The system's table of held meshes is indexed by the scene's slot, so adding and dropping 100,000 entries is
    // linear; a scan per entry would be 5e9 comparisons here and the test would not finish.
    constexpr u32 kCount = 100000;
    InstancedRig rig(kCount);
    const renderer::InstanceSetPtr set = MakeSetOf(2);
    Vector<ecs::Entity> entities;
    for (u32 i = 0; i < kCount; ++i)
    {
        entities.Push(rig.Spawn(Vec3(static_cast<f32>(i % 1000), 0.0f, static_cast<f32>(i / 1000)), set));
    }
    rig.Frame();
    CHECK(rig.render_scene.InstancedCount() == kCount);
    CHECK(rig.Report().refused_full == 0);
    for (u32 i = 0; i < kCount; ++i)
    {
        rig.world.Destroy(entities[i]);
    }
    rig.Frame();
    CHECK(rig.render_scene.InstancedCount() == 0);
}
