// Entity-facing queries (Phase 18 Day 246): entity results equal the key results mapped; ForEachIn visits exactly the
// intersection of the spatial result and the components; destroyed entities do not appear after the sync; batches equal
// the sequential answers (also with workers); the well-known sets answer independently.
#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "jobs/scheduler.h"
#include "os/os.h"
#include "spatial/entity_query.h"
#include "spatial/spatial_sync.h"
#include "spatial_test_rng.h"
#include "spatial_test_util.h"

using namespace vse;
using namespace vse::spatial;
using spatial_test::BoxAt;
using spatial_test::Rng;

namespace
{

struct EqHealth
{
    f64 hp = 10.0f;
};
struct EqDamage
{
    f64 taken = 0.0f;
};

struct EqRig
{
    ecs::World world;
    ecs::Schedule schedule;
    EqRig()
    {
        world.SetResource(ecs::TransformReport{});
        world.SetResource(SpatialWorld());
        schedule.Add(ecs::MakeTransformSystem("transforms"));
        schedule.Add(MakeSpatialSyncSystem("spatial_sync").InStage(ecs::kPostUpdate));
        REQUIRE(schedule.Build().ok);
    }
    void Frame() { schedule.Run(world, 0.016f); }
    SpatialWorld& Spatial() { return world.Resource<SpatialWorld>(); }
    ecs::Entity Spawn(const Vec3d& at, u32 sets)
    {
        ecs::LocalTransform local;
        local.position = at;
        SpatialBounds bounds;
        bounds.local = spatial_test::LocalBoxAt(Vec3(0, 0, 0), Vec3(0.5f, 0.5f, 0.5f));
        SpatialMembership membership;
        membership.sets = static_cast<u8>(sets);
        return world.Create(local, ecs::WorldTransform{}, bounds, membership);
    }
};

Aabbd EqEverything()
{
    return BoxAt(Vec3d(0, 0, 0), Vec3d(1e6f, 1e6f, 1e6f));
}

}  // namespace

TEST_CASE("Entity queries: entity results equal the key results mapped, and the sets answer independently",
          "[spatial][entity]")
{
    EqRig rig;
    Rng rng(11);
    Vector<ecs::Entity> all;
    for (u32 i = 0; i < 300; ++i)
    {
        all.Push(rig.Spawn(rng.Point(-30, 30), 1u + rng.Below(3)));
    }
    for (u32 i = 0; i < 3; ++i)
    {
        rig.Frame();
    }
    for (SetId id = 0; id < 2; ++id)
    {
        Vector<SpatialKey> keys;
        OverlapKeys(*rig.Spatial().Set(id), BoxAt(Vec3d(0, 0, 0), Vec3d(10, 10, 10)), QueryFilter(), Order::Ordered,
                    &keys);
        Vector<ecs::Entity> entities;
        OverlapEntities(rig.Spatial(), id, BoxAt(Vec3d(0, 0, 0), Vec3d(10, 10, 10)), QueryFilter(), &entities);
        REQUIRE(keys.Size() == entities.Size());
        for (usize i = 0; i < keys.Size(); ++i)
        {
            CHECK(entities[i].ToU64() == keys[i]);
        }
    }
    Vector<ecs::Entity> audio;
    CHECK(OverlapEntities(rig.Spatial(), kAudioSet, EqEverything(), QueryFilter(), &audio) == 0);
    CHECK(OverlapEntities(rig.Spatial(), 99, EqEverything(), QueryFilter(), &audio) == 0);
    // Rayd, sweep and nearest agree with the key forms.
    Rayd ray;
    ray.origin = Vec3d(-100, 0, 0);
    ray.direction = Vec3d(1, 0, 0);
    ray.inverse_direction = Vec3d(1, 1e30f, 1e30f);
    EntityHit hit;
    Vector<RayHit> key_hits;
    Raycast(*rig.Spatial().Set(kRenderSet), ray, 1000.0f, QueryFilter(), RayMode::Closest, Order::Ordered, &key_hits);
    const bool found = RaycastEntity(rig.Spatial(), kRenderSet, ray, 1000.0f, QueryFilter(), &hit);
    CHECK(found == (key_hits.Size() == 1));
    if (found)
    {
        CHECK(hit.entity.ToU64() == key_hits[0].key);
    }
    Vector<EntityDistance> nearest;
    CHECK(NearestEntities(rig.Spatial(), kRenderSet, Vec3d(0, 0, 0), 5, 100.0f, QueryFilter(), &nearest) == 5);
    for (usize i = 1; i < nearest.Size(); ++i)
    {
        CHECK(nearest[i - 1].distance <= nearest[i].distance);
    }
}

TEST_CASE(
    "Entity queries: a sweep names the entity, the fraction and the box center there; empty batches and unknown sets answer nothing",
    "[spatial][entity]")
{
    EqRig rig;
    const ecs::Entity wall = rig.Spawn(Vec3d(10, 0, 0), 1);
    for (u32 i = 0; i < 3; ++i)
    {
        rig.Frame();
    }
    EntityHit swept;
    REQUIRE(SweepEntity(rig.Spatial(), kRenderSet, BoxAt(Vec3d(-100, 0, 0), Vec3d(0.2f, 0.2f, 0.2f)), Vec3d(200, 0, 0),
                        QueryFilter(), &swept));
    CHECK(swept.entity == wall);
    CHECK(swept.t > 0.54f);
    CHECK(swept.t < 0.55f);  // (9.5 - 0.2 + 100) / 200
    CHECK(swept.point.x == -100.0f + 200.0f * swept.t);
    CHECK_FALSE(SweepEntity(rig.Spatial(), kRenderSet, BoxAt(Vec3d(-100, 50, 0), Vec3d(0.2f, 0.2f, 0.2f)),
                            Vec3d(200, 0, 0), QueryFilter(), &swept));
    CHECK_FALSE(SweepEntity(rig.Spatial(), 99, BoxAt(Vec3d(-100, 0, 0), Vec3d(0.2f, 0.2f, 0.2f)), Vec3d(200, 0, 0),
                            QueryFilter(), &swept));
    Vector<SpatialKey> keys;
    Vector<u32> offsets;
    OverlapBatch(rig.Spatial(), kRenderSet, Span<const Aabbd>(), QueryFilter(), &keys, &offsets, nullptr);
    CHECK(keys.Size() == 0);
    REQUIRE(offsets.Size() == 1);
    CHECK(offsets[0] == 0);
    Aabbd everything = EqEverything();
    OverlapBatch(rig.Spatial(), 99, Span<const Aabbd>(&everything, 1), QueryFilter(), &keys, &offsets, nullptr);
    CHECK(keys.Size() == 0);
    CHECK(offsets.Size() == 2);
    RaycastBatch(rig.Spatial(), kRenderSet, Span<const Rayd>(), 10.0f, QueryFilter(), Span<BatchRayHit>(), nullptr);
}

TEST_CASE("Entity queries: ForEachIn visits exactly the intersection of the spatial result and the components",
          "[spatial][entity]")
{
    EqRig rig;
    Vector<ecs::Entity> all;
    for (u32 i = 0; i < 40; ++i)
    {
        const ecs::Entity e = rig.Spawn(Vec3d(static_cast<f64>(i), 0, 0), 1);
        if (i % 2 == 0)
        {
            rig.world.Add<EqHealth>(e, EqHealth{});
        }
        if (i % 4 == 0)
        {
            rig.world.Add<EqDamage>(e, EqDamage{});
        }
        all.Push(e);
    }
    for (u32 i = 0; i < 3; ++i)
    {
        rig.Frame();
    }
    const Aabbd region = Aabbd::FromMinMax(Vec3d(9.2f, -1, -1), Vec3d(20.8f, 1, 1));  // entities 10..20
    Vector<ecs::Entity> visited;
    const usize n = ForEachIn<const EqHealth, EqDamage>(rig.world, rig.Spatial(), kRenderSet, region, QueryFilter(),
                                                        [&](ecs::Entity e, const EqHealth& h, EqDamage& d)
                                                        {
                                                            d.taken += h.hp;
                                                            visited.Push(e);
                                                        });
    // Entities 10..20 that have Health and Damage: multiples of 4: 12, 16, 20.
    REQUIRE(n == 3);
    CHECK(visited[0] == all[12]);
    CHECK(visited[1] == all[16]);
    CHECK(visited[2] == all[20]);
    CHECK(rig.world.Get<EqDamage>(all[16])->taken == 10.0f);
    CHECK(rig.world.Get<EqDamage>(all[0])->taken == 0.0f);
}

TEST_CASE("Entity queries: a destroyed entity does not appear after the sync", "[spatial][entity]")
{
    EqRig rig;
    const ecs::Entity a = rig.Spawn(Vec3d(0, 0, 0), 1);
    const ecs::Entity b = rig.Spawn(Vec3d(1, 0, 0), 1);
    for (u32 i = 0; i < 3; ++i)
    {
        rig.Frame();
    }
    REQUIRE(rig.world.Destroy(a) == ecs::Status::Ok);
    for (u32 i = 0; i < 3; ++i)
    {
        rig.Frame();
    }
    Vector<ecs::Entity> found;
    OverlapEntities(rig.Spatial(), kRenderSet, EqEverything(), QueryFilter(), &found);
    REQUIRE(found.Size() == 1);
    CHECK(found[0] == b);
}

TEST_CASE("Entity queries: ray and overlap batches equal the sequential answers, with workers too", "[spatial][entity]")
{
    EqRig rig;
    Rng rng(5);
    for (u32 i = 0; i < 2000; ++i)
    {
        rig.Spawn(rng.Point(-100, 100), 1);
    }
    for (u32 i = 0; i < 3; ++i)
    {
        rig.Frame();
    }
    constexpr u32 kQueries = 5000;
    Vector<Rayd> rays;
    Vector<Aabbd> boxes;
    for (u32 i = 0; i < kQueries; ++i)
    {
        Rayd ray;
        ray.origin = rng.Point(-120, 120);
        ray.direction = Vec3d(rng.Point(-1, 1) + Vec3(0.01f, 0.01f, 0.01f));
        ray.inverse_direction = Vec3d(1.0f / ray.direction.x, 1.0f / ray.direction.y, 1.0f / ray.direction.z);
        rays.Push(ray);
        boxes.Push(BoxAt(rng.Point(-100, 100), Vec3d(3, 3, 3)));
    }
    jobs::Scheduler::Config config;
    config.participants = 4;
    config.pin = false;
    jobs::Scheduler scheduler(config);
    Vector<BatchRayHit> seq, par;
    seq.Resize(kQueries);
    par.Resize(kQueries);
    RaycastBatch(rig.Spatial(), kRenderSet, Span<const Rayd>(rays.Data(), rays.Size()), 500.0f, QueryFilter(),
                 Span<BatchRayHit>(seq.Data(), seq.Size()), nullptr);
    const u64 start = os::MonotonicTimeNanos();
    RaycastBatch(rig.Spatial(), kRenderSet, Span<const Rayd>(rays.Data(), rays.Size()), 500.0f, QueryFilter(),
                 Span<BatchRayHit>(par.Data(), par.Size()), &scheduler);
    WARN("a batch of " << kQueries << " rays with 4 participants took "
                       << static_cast<f64>(os::MonotonicTimeNanos() - start) / 1e6 << " ms");
    u32 hits = 0;
    for (u32 i = 0; i < kQueries; ++i)
    {
        REQUIRE(seq[i].hit == par[i].hit);
        REQUIRE(seq[i].key == par[i].key);
        REQUIRE(seq[i].t == par[i].t);
        // Against the single-ray entity query.
        EntityHit one;
        const bool h = RaycastEntity(rig.Spatial(), kRenderSet, rays[i], 500.0f, QueryFilter(), &one);
        REQUIRE(h == seq[i].hit);
        if (h)
        {
            REQUIRE(one.entity.ToU64() == seq[i].key);
        }
        hits += seq[i].hit ? 1u : 0u;
    }
    CHECK(hits > 0);
    Vector<SpatialKey> keys_a, keys_b;
    Vector<u32> off_a, off_b;
    OverlapBatch(rig.Spatial(), kRenderSet, Span<const Aabbd>(boxes.Data(), boxes.Size()), QueryFilter(), &keys_a,
                 &off_a, nullptr);
    OverlapBatch(rig.Spatial(), kRenderSet, Span<const Aabbd>(boxes.Data(), boxes.Size()), QueryFilter(), &keys_b,
                 &off_b, &scheduler);
    REQUIRE(off_a.Size() == kQueries + 1);
    REQUIRE(off_a.Size() == off_b.Size());
    REQUIRE(keys_a.Size() == keys_b.Size());
    for (usize i = 0; i < off_a.Size(); ++i)
    {
        REQUIRE(off_a[i] == off_b[i]);
    }
    for (usize i = 0; i < keys_a.Size(); ++i)
    {
        REQUIRE(keys_a[i] == keys_b[i]);
    }
    // Each query's keys match the single query.
    for (u32 i = 0; i < 50; ++i)
    {
        Vector<ecs::Entity> one;
        OverlapEntities(rig.Spatial(), kRenderSet, boxes[i], QueryFilter(), &one);
        REQUIRE(one.Size() == off_a[i + 1] - off_a[i]);
        for (usize k = 0; k < one.Size(); ++k)
        {
            REQUIRE(one[k].ToU64() == keys_a[off_a[i] + k]);
        }
    }
}
