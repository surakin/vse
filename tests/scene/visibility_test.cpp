// Phase 18 Day 247: RenderScene::Cull through the bounding-volume hierarchy gives the flat scan's slots, in the same
// order, on random scenes with moves, removals, hidden objects and objects with boxes the hierarchy cannot hold; the
// cost of both paths on a 100,000-object scene is recorded.
#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "lang/limits.h"
#include "os/os.h"
#include "renderer/camera.h"
#include "renderer/frustum.h"
#include "renderer/mesh.h"
#include "renderer/render_scene.h"
#include "rhi/null/null_device.h"

using namespace engine_placeholder;

namespace
{

struct VisRng
{
    u64 state;
    explicit VisRng(u64 seed) : state(seed) {}
    u32 Next()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<u32>(state >> 33);
    }
    f32 Unit() { return static_cast<f32>(Next() & 0xFFFFFF) / static_cast<f32>(1u << 24); }
    f32 Range(f32 lo, f32 hi) { return lo + (hi - lo) * Unit(); }
};

Mat4 VisAt(const Vec3& p)
{
    return Mat4::FromBasis(Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), p);
}

renderer::Frustum VisFrustum(VisRng& rng)
{
    renderer::Camera camera = renderer::MakeLookAtCamera(Vec3(rng.Range(-80, 80), rng.Range(0, 40), rng.Range(-80, 80)),
                                                         Vec3(rng.Range(-40, 40), 0, rng.Range(-40, 40)),
                                                         Vec3(0, 1, 0));
    camera.far_plane = rng.Range(30.0f, 300.0f);
    return renderer::Frustum::FromViewProjection(camera.ViewProjection());
}

bool VisSame(const renderer::RenderScene& scene, const renderer::Frustum& frustum, Vector<u32>* a, Vector<u32>* b)
{
    a->Resize(scene.SlotCount() + 1);
    b->Resize(scene.SlotCount() + 1);
    const u32 n = scene.Cull(frustum, a->Data(), scene.SlotCount() + 1);
    renderer::RenderScene& mutable_scene = const_cast<renderer::RenderScene&>(scene);
    mutable_scene.SetCullMode(renderer::RenderScene::CullMode::Flat);
    const u32 m = scene.Cull(frustum, b->Data(), scene.SlotCount() + 1);
    mutable_scene.SetCullMode(renderer::RenderScene::CullMode::Bvh);
    if (n != m)
    {
        return false;
    }
    for (u32 i = 0; i < n; ++i)
    {
        if ((*a)[i] != (*b)[i])
        {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("Culling: the hierarchy gives the flat scan's slots in 1,000 random scenes with changes",
          "[scene][visibility]")
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    renderer::MeshData data;
    REQUIRE(renderer::MakeCubeMesh(1.0f, &data) == nullptr);
    renderer::GpuMesh mesh;
    REQUIRE(mesh.Create(device.Get(), data, "cube"));
    Vector<u32> a, b;
    u32 visible_total = 0;
    for (u32 scene_index = 0; scene_index < 1000; ++scene_index)
    {
        VisRng rng(1000 + scene_index);
        renderer::RenderSceneDesc desc;
        desc.max_objects = 200;
        desc.max_lights = 1;
        renderer::RenderScene scene(desc);
        Vector<renderer::ObjectId> ids;
        const u32 count = 5 + rng.Next() % 60;
        for (u32 i = 0; i < count; ++i)
        {
            renderer::RenderObjectDesc od;
            od.mesh = &mesh;
            od.transform = VisAt(Vec3(rng.Range(-100, 100), rng.Range(-5, 20), rng.Range(-100, 100)));
            od.flags = rng.Next() % 8 == 0 ? renderer::ObjectFlags::kHidden : 0u;
            ids.Push(scene.AddObject(od));
        }
        for (u32 step = 0; step < 6; ++step)
        {
            for (u32 k = 0; k < 6 && ids.Size() > 0; ++k)
            {
                const usize i = rng.Next() % ids.Size();
                switch (rng.Next() % 6)
                {
                case 0:
                    scene.RemoveObject(ids[i]);
                    ids[i] = ids[ids.Size() - 1];
                    ids.Pop();
                    break;
                case 1: scene.SetFlags(ids[i], rng.Next() % 2 == 0 ? renderer::ObjectFlags::kHidden : 0u); break;
                case 2:
                {  // a box the hierarchy cannot hold, and back
                    scene.SetTransform(ids[i], VisAt(Vec3(Limits<f32>::QuietNaN(), 0, 0)));
                    break;
                }
                case 3:
                {
                    renderer::RenderObjectDesc od;
                    od.mesh = &mesh;
                    od.transform = VisAt(Vec3(rng.Range(-100, 100), 0, rng.Range(-100, 100)));
                    const renderer::ObjectId id = scene.AddObject(od);
                    if (id.IsValid())
                    {
                        ids.Push(id);
                    }
                    break;
                }
                default:
                    scene.SetTransform(ids[i],
                                       VisAt(Vec3(rng.Range(-100, 100), rng.Range(-5, 20), rng.Range(-100, 100))));
                    break;
                }
            }
            const renderer::Frustum frustum = VisFrustum(rng);
            REQUIRE(VisSame(scene, frustum, &a, &b));
            const u32 n = scene.Cull(frustum, a.Data(), scene.SlotCount() + 1);
            visible_total += n;
        }
    }
    CHECK(visible_total > 1000);
}

TEST_CASE(
    "Culling: a moved object is culled and unculled in the same frame as the flat scan, a hidden one is never visible",
    "[scene][visibility]")
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    renderer::MeshData data;
    REQUIRE(renderer::MakeCubeMesh(1.0f, &data) == nullptr);
    renderer::GpuMesh mesh;
    REQUIRE(mesh.Create(device.Get(), data, "cube"));
    renderer::RenderSceneDesc desc;
    desc.max_objects = 8;
    desc.max_lights = 1;
    renderer::RenderScene scene(desc);
    renderer::RenderObjectDesc od;
    od.mesh = &mesh;
    od.transform = VisAt(Vec3(0, 0, 0));
    const renderer::ObjectId id = scene.AddObject(od);
    renderer::Camera camera = renderer::MakeLookAtCamera(Vec3(0, 0, 10), Vec3(0, 0, 0), Vec3(0, 1, 0));
    const renderer::Frustum frustum = renderer::Frustum::FromViewProjection(camera.ViewProjection());
    Vector<u32> a, b;
    CHECK(scene.Cull(frustum, a.Data() == nullptr ? (a.Resize(8), a.Data()) : a.Data(), 8) == 1);
    scene.SetTransform(id, VisAt(Vec3(1000, 0, 0)));
    REQUIRE(VisSame(scene, frustum, &a, &b));
    CHECK(scene.Cull(frustum, a.Data(), 8) == 0);
    scene.SetTransform(id, VisAt(Vec3(0, 0, 0)));
    CHECK(scene.Cull(frustum, a.Data(), 8) == 1);
    scene.SetFlags(id, renderer::ObjectFlags::kHidden);
    CHECK(scene.Cull(frustum, a.Data(), 8) == 0);
}

TEST_CASE("Culling a 100,000-object scene: the hierarchy against the flat scan (recorded)", "[scene][visibility][cost]")
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    renderer::MeshData data;
    REQUIRE(renderer::MakeCubeMesh(1.0f, &data) == nullptr);
    renderer::GpuMesh mesh;
    REQUIRE(mesh.Create(device.Get(), data, "cube"));
    constexpr u32 kCount = 100000;
    renderer::RenderSceneDesc desc;
    desc.max_objects = kCount;
    desc.max_lights = 1;
    renderer::RenderScene scene(desc);
    VisRng rng(3);
    for (u32 i = 0; i < kCount; ++i)
    {
        renderer::RenderObjectDesc od;
        od.mesh = &mesh;
        od.transform = VisAt(Vec3(rng.Range(-300, 300), rng.Range(0, 20), rng.Range(-300, 300)));
        REQUIRE(scene.AddObject(od).IsValid());
    }
    renderer::Camera camera = renderer::MakeLookAtCamera(Vec3(-60, 10, -60), Vec3(-30, 5, -30), Vec3(0, 1, 0));
    camera.far_plane = 60.0f;  // a camera that sees a few percent of the scene
    const renderer::Frustum frustum = renderer::Frustum::FromViewProjection(camera.ViewProjection());
    Vector<u32> slots;
    slots.Resize(kCount);
    f64 best[2] = {1e30, 1e30};
    u32 visible[2] = {0, 0};
    for (u32 mode = 0; mode < 2; ++mode)
    {
        scene.SetCullMode(mode == 0 ? renderer::RenderScene::CullMode::Bvh : renderer::RenderScene::CullMode::Flat);
        for (u32 run = 0; run < 10; ++run)
        {
            const u64 start = os::MonotonicTimeNanos();
            visible[mode] = scene.Cull(frustum, slots.Data(), kCount);
            const f64 ms = static_cast<f64>(os::MonotonicTimeNanos() - start) / 1e6;
            best[mode] = ms < best[mode] ? ms : best[mode];
        }
    }
    CHECK(visible[0] == visible[1]);
    WARN("100,000 objects, " << visible[0] << " visible: hierarchy " << best[0] << " ms, flat " << best[1] << " ms, "
                             << best[1] / best[0] << "x");
}

TEST_CASE("Culling: a frustum with a plane that is not finite gives the flat scan's slots in both modes",
          "[scene][visibility]")
{
    rhi::NullDevicePtr device = rhi::CreateNullDevice();
    renderer::MeshData data;
    REQUIRE(renderer::MakeCubeMesh(1.0f, &data) == nullptr);
    renderer::GpuMesh mesh;
    REQUIRE(mesh.Create(device.Get(), data, "cube"));
    renderer::RenderSceneDesc desc;
    desc.max_objects = 8;
    desc.max_lights = 1;
    renderer::RenderScene scene(desc);
    renderer::RenderObjectDesc od;
    od.mesh = &mesh;
    od.transform = VisAt(Vec3(0, 0, 0));
    REQUIRE(scene.AddObject(od).IsValid());
    renderer::Camera camera = renderer::MakeLookAtCamera(Vec3(0, 0, 10), Vec3(0, 0, 0), Vec3(0, 1, 0));
    renderer::Frustum frustum = renderer::Frustum::FromViewProjection(camera.ViewProjection());
    frustum.planes[0].normal.x = Limits<f32>::QuietNaN();
    Vector<u32> a, b;
    REQUIRE(VisSame(scene, frustum, &a, &b));
}
