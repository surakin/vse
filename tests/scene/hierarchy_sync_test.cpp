// Instance hierarchies in the scene: the sync mirrors InstanceHierarchy entities into the render scene, recomposes only
// the marked subtrees, and the wind system sways flagged nodes deterministically. CPU only, on the null device.
#include <catch2/catch_test_macros.hpp>

#include "containers/shared_ptr.h"
#include "containers/vector.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "renderer/mesh.h"
#include "renderer/render_scene.h"
#include "rhi/null/null_device.h"
#include "scene/instance_wind.h"
#include "scene/render_sync.h"

using namespace vse;
using scene::Hidden;
using scene::HierarchyHandle;
using scene::InstanceHierarchy;
using scene::SceneLink;
using scene::SceneSyncReport;

namespace
{

renderer::GpuMeshPtr HsMesh(rhi::Device* device)
{
    renderer::MeshData data;
    REQUIRE(renderer::MakeSphereMesh(1.0f, 8, 4, &data) == nullptr);
    renderer::GpuMeshPtr mesh = MakeShared<renderer::GpuMesh>();
    REQUIRE(mesh->Create(device, data, "sphere"));
    return mesh;
}

// A trunk with `branches` branches, each with `twigs` twigs; branches carry the wind flag.
renderer::InstanceTreePtr HsTree(u32 branches, u32 twigs)
{
    Vector<renderer::TreeMeshInfo> meshes;
    renderer::TreeMeshInfo m;
    m.bounds = Aabb::FromMinMax(Vec3(-1.0f, -1.0f, -1.0f), Vec3(1.0f, 1.0f, 1.0f));
    meshes.Push(m);
    renderer::InstanceTreeBuilder builder(Span<const renderer::TreeMeshInfo>(meshes.Data(), meshes.Size()));
    renderer::TreeNodeDesc trunk;
    trunk.mesh = 0;
    renderer::TreeNodeId root;
    REQUIRE(builder.AddRoot(trunk, &root) == renderer::TreeStatus::Ok);
    for (u32 b = 0; b < branches; ++b)
    {
        renderer::TreeNodeDesc branch;
        branch.mesh = 0;
        branch.position = Vec3d(static_cast<f64>(b), 2.0, 0.0);
        branch.scale = Vec3(0.5f, 0.5f, 0.5f);
        branch.flags = scene::kTreeNodeWind;
        renderer::TreeNodeId bid;
        REQUIRE(builder.AddChild(root, branch, &bid) == renderer::TreeStatus::Ok);
        for (u32 t = 0; t < twigs; ++t)
        {
            renderer::TreeNodeDesc twig;
            twig.mesh = 0;
            twig.position = Vec3d(0.0, 1.0 + static_cast<f64>(t), 0.0);
            twig.scale = Vec3(0.5f, 0.5f, 0.5f);
            REQUIRE(builder.AddChild(bid, twig, nullptr) == renderer::TreeStatus::Ok);
        }
    }
    renderer::InstanceTreePtr tree = builder.Finish(nullptr);
    REQUIRE(tree.Get() != nullptr);
    return tree;
}

struct HierarchyRig
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    renderer::RenderScene render_scene;
    ecs::World world;
    ecs::Schedule schedule;
    renderer::GpuMeshPtr mesh;

    HierarchyRig() : render_scene(renderer::RenderSceneDesc{16, 4, 2, 4})
    {
        mesh = HsMesh(device.Get());
        world.SetResource(ecs::TransformReport{});
        world.SetResource(SceneLink{&render_scene});
        world.SetResource(SceneSyncReport{});
        world.SetResource(scene::WindState{});
        schedule.Add(ecs::MakeTransformSystem("transforms"));
        schedule.Add(scene::MakeWindSystem("wind").InStage(ecs::kRender));
        schedule.Add(scene::MakeHierarchySyncSystem("hierarchy_sync").InStage(ecs::kRender));
        REQUIRE(schedule.Build().ok);
    }
    void Frame() { schedule.Run(world, 0.016f); }
    const SceneSyncReport& Report() { return world.Resource<SceneSyncReport>(); }
    ecs::Entity Spawn(const Vec3& position, const renderer::InstanceTreePtr& tree)
    {
        ecs::LocalTransform t;
        t.position = position;
        InstanceHierarchy component;
        component.tree = tree;
        component.meshes[0] = mesh;
        component.materials[0] = 1;
        return world.Create(t, ecs::WorldTransform{}, component);
    }
    const renderer::InstancedObject* ObjectOf(ecs::Entity e)
    {
        const HierarchyHandle* handle = world.Get<HierarchyHandle>(e);
        return handle == nullptr ? nullptr : render_scene.FindInstanced(handle->id);
    }
};

}  // namespace

TEST_CASE("Hierarchy sync: an entity with a tree is mirrored, moves with the entity, hides and goes away",
          "[scene][instancing]")
{
    HierarchyRig rig;
    renderer::InstanceTreePtr tree = HsTree(3, 2);
    const ecs::Entity e = rig.Spawn(Vec3(1, 2, 3), tree);
    rig.Frame();
    rig.Frame();
    REQUIRE(rig.ObjectOf(e) != nullptr);
    CHECK(rig.Report().hierarchies_added == 1);
    const renderer::InstancedObject* object = rig.ObjectOf(e);
    CHECK(object->tree.Get() == tree.Get());
    CHECK(object->set.Get() == nullptr);
    CHECK(object->tree_meshes[0] == rig.mesh.Get());
    CHECK(object->tree_materials[0] == 1);
    CHECK(object->position.x == 1.0f);
    CHECK(object->position.z == 3.0f);
    // Moving the entity moves the whole tree without touching it.
    ecs::LocalTransform* local = rig.world.Get<ecs::LocalTransform>(e);
    local->position = Vec3(10, 0, 0);
    rig.Frame();
    rig.Frame();
    CHECK(rig.ObjectOf(e)->position.x == 10.0f);
    CHECK(rig.Report().hierarchies_set >= 1);
    // Hidden hides it.
    rig.world.Add<Hidden>(e, Hidden{});
    rig.Frame();
    rig.Frame();
    CHECK((rig.ObjectOf(e)->flags & renderer::ObjectFlags::kHidden) != 0);
    rig.world.Remove<Hidden>(e);
    rig.Frame();
    rig.Frame();
    CHECK((rig.ObjectOf(e)->flags & renderer::ObjectFlags::kHidden) == 0);
    // Destroying the entity removes it from the scene.
    rig.world.Destroy(e);
    rig.Frame();
    rig.Frame();
    CHECK(rig.render_scene.InstancedCount() == 0);
    CHECK(rig.Report().hierarchies_removed == 1);
}

TEST_CASE("Hierarchy sync: an invalid tree is counted, not drawn", "[scene][instancing]")
{
    HierarchyRig rig;
    ecs::LocalTransform t;
    InstanceHierarchy empty;  // no tree
    const ecs::Entity none = rig.world.Create(t, ecs::WorldTransform{}, empty);
    InstanceHierarchy no_mesh;  // the tree names a mesh the component does not hold
    no_mesh.tree = HsTree(1, 1);
    const ecs::Entity missing = rig.world.Create(t, ecs::WorldTransform{}, no_mesh);
    rig.Frame();
    CHECK(rig.ObjectOf(none) == nullptr);
    CHECK(rig.ObjectOf(missing) == nullptr);
    CHECK(rig.Report().invalid >= 2);
    CHECK(rig.render_scene.InstancedCount() == 0);
}

TEST_CASE("Hierarchy sync: the wind recomposes only the swaying subtrees", "[scene][instancing]")
{
    HierarchyRig rig;
    // 20 branches of 4 twigs: 1 + 20 * 5 = 101 nodes; every branch sways.
    renderer::InstanceTreePtr tree = HsTree(20, 4);
    const ecs::Entity e = rig.Spawn(Vec3(0, 0, 0), tree);
    rig.Frame();  // the wind sways every branch at time 0; the finished tree already holds the trunk
    CHECK(rig.Report().hierarchy_nodes_updated == tree->NodeCount() - 1);
    scene::WindState wind;
    wind.time = 1.0f;
    rig.world.SetResource(wind);
    const u64 before = rig.Report().hierarchy_nodes_updated;
    rig.Frame();
    // The trunk (the root) does not sway: only the 20 branches and their 80 twigs are recomposed.
    CHECK(rig.Report().hierarchy_nodes_updated - before == 100);
    // The angle is a function of the time and the node id: two trees swayed at the same time agree.
    renderer::InstanceTreePtr other = HsTree(20, 4);
    HierarchyRig rig2;
    rig2.Spawn(Vec3(0, 0, 0), other);
    rig2.world.SetResource(wind);
    rig2.Frame();
    rig2.Frame();
    u32 different = 0;
    for (u32 i = 0; i < tree->NodeCount(); ++i)
    {
        const Quaternion a = tree->Node(i).world.rotation;
        const Quaternion b = other->Node(i).world.rotation;
        different += (a.x != b.x || a.y != b.y || a.z != b.z || a.w != b.w) ? 1u : 0u;
    }
    CHECK(different == 0);
    // A flagged node actually moved.
    CHECK(tree->Node(1).world.rotation.x != 0.0f);
    CHECK(tree->Node(0).world.rotation.x == 0.0f);
    (void)e;
}

TEST_CASE("Wind angle: a function of time and id only", "[scene][instancing]")
{
    scene::WindState wind;
    wind.time = 0.37f;
    wind.amplitude = 0.2f;
    wind.frequency = 1.5f;
    CHECK(scene::WindAngle(wind, 5) == scene::WindAngle(wind, 5));
    CHECK(scene::WindAngle(wind, 5) != scene::WindAngle(wind, 6));
    wind.amplitude = 0.0f;
    CHECK(scene::WindAngle(wind, 5) == 0.0f);
}
