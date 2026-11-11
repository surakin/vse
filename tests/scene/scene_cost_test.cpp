// Phase 17 Day 234: the parallel-culling question. Recorded, not asserted: run with `scene_tests "[cost]"` in a Release
// build. Compares RenderScene::Cull (one thread, 100k objects) with a chunk-parallel frustum test over a bounds
// component in the ECS.
#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "ecs/query.h"
#include "ecs/world.h"
#include "jobs/scheduler.h"
#include "os/console.h"
#include "os/os.h"
#include "renderer/camera.h"
#include "renderer/frustum.h"
#include "renderer/mesh.h"
#include "renderer/render_scene.h"
#include "rhi/null/null_device.h"

using namespace engine_placeholder;

namespace
{

struct WorldBounds
{
    renderer::Bounds bounds;
};

struct Lcg
{
    u32 state = 12345;
    f32 Unit()
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<f32>(state >> 8) / static_cast<f32>(1u << 24);
    }
};

double Millis(u64 nanos)
{
    return static_cast<double>(nanos) / 1e6;
}

}  // namespace

TEST_CASE("Culling 100k objects: one thread against chunk-parallel (recorded)", "[scene][cost][.]")
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
    ecs::World world;
    Lcg rng;
    for (u32 i = 0; i < kCount; ++i)
    {
        renderer::RenderObjectDesc od;
        od.mesh = &mesh;
        od.transform = Mat4::FromBasis(
            Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1),
            Vec3(rng.Unit() * 600.0f - 300.0f, rng.Unit() * 20.0f, rng.Unit() * 600.0f - 300.0f));
        const renderer::ObjectId id = scene.AddObject(od);
        REQUIRE(id.IsValid());
        world.Create(WorldBounds{scene.FindObject(id)->bounds});
    }
    renderer::Camera camera = renderer::MakeLookAtCamera(Vec3(0, 30, 320), Vec3(0, 0, 0), Vec3(0, 1, 0));
    camera.far_plane = 700.0f;
    const renderer::Frustum frustum = renderer::Frustum::FromViewProjection(camera.ViewProjection());

    Vector<u32> slots;
    slots.Resize(kCount);
    u64 best_single = ~u64{0};
    u32 visible = 0;
    for (int run = 0; run < 20; ++run)
    {
        const u64 t0 = os::MonotonicTimeNanos();
        visible = scene.Cull(frustum, slots.Data(), kCount);
        const u64 t = os::MonotonicTimeNanos() - t0;
        best_single = t < best_single ? t : best_single;
    }
    os::Print("cull, one thread: %u of %u visible, %.3f ms (%.1f ns per object)\n", visible, kCount,
              Millis(best_single), static_cast<double>(best_single) / kCount);

    auto query = world.Query<ecs::Read<WorldBounds>>();
    auto per_chunk = [&frustum](ecs::ChunkView& view)
    {
        Span<const WorldBounds> bounds = view.Read<WorldBounds>();
        u64 n = 0;
        for (usize i = 0; i < bounds.Size(); ++i)
        {
            n += frustum.Visible(bounds[i].bounds) ? 1u : 0u;
        }
        return n;
    };
    auto combine = [](u64 a, u64 b) { return a + b; };
    for (usize participants : {usize{1}, usize{2}, usize{4}, usize{8}, usize{16}})
    {
        jobs::Scheduler::Config config;
        config.participants = participants;
        config.pin = false;
        jobs::Scheduler jobs(config);
        u64 best = ~u64{0};
        u64 counted = 0;
        for (int run = 0; run < 20; ++run)
        {
            const u64 t0 = os::MonotonicTimeNanos();
            counted = query.ReduceChunks<u64>(&jobs, 0, per_chunk, combine, 0);
            const u64 t = os::MonotonicTimeNanos() - t0;
            best = t < best ? t : best;
        }
        os::Print("cull, chunk-parallel, %zu participants: %llu visible, %.3f ms (%.1f ns per object)\n", participants,
                  static_cast<unsigned long long>(counted), Millis(best), static_cast<double>(best) / kCount);
        CHECK(counted == visible);
    }
}
