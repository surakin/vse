// Phase 18 Day 248: the benchmark of the spatial module against the pass conditions of
// docs/superpowers/specs/2026-11-10-spatial-design.md, section 4. Hidden (`[.]`): run it by name in a Release tree,
// `spatial_tests "[spatial-bench]"`. It prints one line per measurement and checks only that the answers are right (the
// numbers are recorded in the day file, not gated, because they depend on the machine).
#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "log/output.h"
#include "cstr/format.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "ecs/world.h"
#include "jobs/scheduler.h"
#include "lang/scalar.h"
#include "math/geometry.h"
#include "os/console.h"
#include "os/os.h"
#include "spatial/components.h"
#include "spatial/pair_tracker.h"
#include "spatial/query.h"
#include "spatial/spatial_set.h"
#include "spatial/spatial_sync.h"
#include "spatial/spatial_world.h"
#include "spatial_test_rng.h"
#include "spatial_test_util.h"

using namespace vse;
using namespace vse::spatial;
using spatial_test::BoxAt;
using spatial_test::Rng;

namespace
{

f64 NowMs()
{
    return static_cast<f64>(os::MonotonicTimeNanos()) / 1e6;
}

void Say(const char* name, u32 n, f64 value, const char* unit)
{
    out::Print("BENCH %-44s n=%-8u %12.4f %s\n", name, n, value, unit);
}

// A city: `n` boxes of about 1 unit spread over a square whose density is fixed (about one item per 8 square units, 3
// units tall).
struct City
{
    Vector<Aabbd> boxes;
    f64 half = 0.0f;
    explicit City(u32 n, u64 seed)
    {
        Rng rng(seed);
        half = Sqrt(static_cast<f64>(n) * 8.0f) * 0.5f;
        for (u32 i = 0; i < n; ++i)
        {
            boxes.Push(
                BoxAt(Vec3d(rng.Float(-half, half), rng.Float(0, 3), rng.Float(-half, half)), Vec3d(0.5f, 0.5f, 0.5f)));
        }
    }
};

// A camera that sees about `fraction` of the square: a pyramid looking down -z from the edge, widened until the share
// of boxes seen is about right.
Frustumd ViewOf(const City& city, f64 fraction)
{
    const f64 side = city.half * 2.0f * Sqrt(fraction);
    const Vec3d apex(0, 1.5f, city.half);
    const f64 reach = side;  // the far plane
    const f64 w = 0.5f * side / reach;
    Frustumd f;
    const f64 inv = 1.0f / Sqrt(1.0f + w * w);
    f.planes[0] = Planed::FromPointNormal(apex, Vec3d(inv, 0, -w * inv));
    f.planes[1] = Planed::FromPointNormal(apex, Vec3d(-inv, 0, -w * inv));
    f.planes[2] = Planed::FromPointNormal(apex - Vec3d(0, 100, 0), Vec3d(0, 1, 0));
    f.planes[3] = Planed::FromPointNormal(apex + Vec3d(0, 100, 0), Vec3d(0, -1, 0));
    f.planes[4] = Planed::FromPointNormal(apex + Vec3d(0, 0, -0.1f), Vec3d(0, 0, -1));
    f.planes[5] = Planed::FromPointNormal(apex + Vec3d(0, 0, -reach), Vec3d(0, 0, 1));
    return f;
}

u32 FlatCount(const City& city, const Frustumd& f)
{
    u32 n = 0;
    for (usize i = 0; i < city.boxes.Size(); ++i)
    {
        if (f.TestAabb(city.boxes[i]) != Containment::Outside)
        {
            ++n;
        }
    }
    return n;
}

}  // namespace

TEST_CASE("Spatial benchmark: culling, static queries and memory", "[.][spatial-bench]")
{
    const u32 sizes[4] = {1000, 10000, 100000, 1000000};
    for (u32 n : sizes)
    {
        City city(n, 11);
        SpatialSet set;
        const f64 t0 = NowMs();
        for (u32 i = 0; i < n; ++i)
        {
            set.AddStatic(city.boxes[i], i, 0);
        }
        set.RebuildStatic();
        Say("static build", n, NowMs() - t0, "ms");

        const Frustumd f1 = ViewOf(city, 0.01f);
        u32 flat = 0, tree = 0;
        const u32 reps = n >= 100000 ? 20 : 200;
        f64 t = NowMs();
        for (u32 r = 0; r < reps; ++r)
        {
            flat = FlatCount(city, f1);
        }
        const f64 flat_ms = (NowMs() - t) / reps;
        t = NowMs();
        for (u32 r = 0; r < reps; ++r)
        {
            tree = 0;
            Overlap(set, f1, QueryFilter(),
                    [&](const Item&)
                    {
                        ++tree;
                        return true;
                    });
        }
        const f64 tree_ms = (NowMs() - t) / reps;
        Say("cull flat scan, ~1% visible", n, flat_ms, "ms");
        Say("cull tree, ~1% visible", n, tree_ms, "ms");
        Say("cull gain (flat / tree)", n, flat_ms / tree_ms, "x");
        CHECK(tree <= flat);  // the tree tests the exact box, the flat scan the conservative box test of Frustumd: same
                              // or fewer (never more here)
        CHECK(tree >= flat * 9 / 10);

        // A quiet scene (a camera that sees nothing new): the cost of a static set with nothing to do is zero structure
        // calls (Day 245); here, a cull of a small view against the flat scan.
        const Frustumd f0 = ViewOf(city, 0.0004f);
        t = NowMs();
        for (u32 r = 0; r < reps; ++r)
        {
            Overlap(set, f0, QueryFilter(), [&](const Item&) { return true; });
        }
        Say("cull tree, tiny view", n, (NowMs() - t) / reps, "ms");

        // Overlap and ray per query on the static tree.
        Rng rng(5);
        const u32 queries = 20000;
        u32 sink = 0;
        t = NowMs();
        for (u32 q = 0; q < queries; ++q)
        {
            const Aabbd box = BoxAt(Vec3d(rng.Float(-city.half, city.half), 1.5f, rng.Float(-city.half, city.half)),
                                    Vec3d(2, 2, 2));
            Overlap(set, box, QueryFilter(),
                    [&](const Item&)
                    {
                        ++sink;
                        return true;
                    });
        }
        Say("overlap box 4x4x4 per query", n, (NowMs() - t) * 1000.0 / queries, "us");
        t = NowMs();
        for (u32 q = 0; q < queries; ++q)
        {
            const Aabbd box = BoxAt(Vec3d(rng.Float(-city.half, city.half), 1.5f, rng.Float(-city.half, city.half)),
                                    Vec3d(20, 20, 20));  // the spec's 40-unit query
            Overlap(set, box, QueryFilter(),
                    [&](const Item&)
                    {
                        ++sink;
                        return true;
                    });
        }
        Say("overlap box 40x40x40 per query", n, (NowMs() - t) * 1000.0 / queries, "us");
        Vector<RayHit> hits;
        t = NowMs();
        for (u32 q = 0; q < queries; ++q)
        {
            const Rayd ray = Rayd::Make(Vec3d(rng.Float(-city.half, city.half), 1.5f, rng.Float(-city.half, city.half)),
                                        Vec3d(1, 0, 0.1f));
            Raycast(set, ray, 50.0f, QueryFilter(), RayMode::Closest, Order::Ordered, &hits);
            sink += static_cast<u32>(hits.Size());
        }
        Say("ray closest, 50 long, per query", n, (NowMs() - t) * 1000.0 / queries, "us");
        CHECK(sink > 0);
        Say("static tree nodes", n, static_cast<f64>(set.StaticTree().NodeCount()), "nodes");
    }
}

TEST_CASE("Spatial benchmark: moving agents, tree against grid, with the pair feed", "[.][spatial-bench]")
{
    const u32 sizes[3] = {1000, 10000, 100000};
    for (u32 n : sizes)
    {
        for (int kind = 0; kind < 2; ++kind)
        {
            SpatialSetConfig config;
            if (kind == 1)
            {
                config.dynamic = DynamicKind::Grid;
                config.grid_cell_size = 2.0f;
            }
            City city(n, 21);
            SpatialSet set(config);
            Vector<SetHandle> handles;
            for (u32 i = 0; i < n; ++i)
            {
                handles.Push(set.AddDynamic(city.boxes[i], i, 0));
            }
            const char* name = kind == 0 ? "tree" : "grid";
            Rng rng(3);
            for (u32 percent : {1u, 100u})
            {
                const u32 moving = n * percent / 100;
                const u32 frames = percent == 100 ? 5 : 20;
                const f64 t0 = NowMs();
                for (u32 fr = 0; fr < frames; ++fr)
                {
                    for (u32 i = 0; i < moving; ++i)
                    {
                        Aabbd& b = city.boxes[i];
                        const Vec3d d(rng.Float(-0.3f, 0.3f), 0, rng.Float(-0.3f, 0.3f));
                        b = Aabbd::FromMinMax(b.min + d, b.max + d);
                        set.Move(handles[i], b);
                    }
                }
                const f64 ms = (NowMs() - t0) / frames;
                char label[96];
                FormatTo(label, sizeof(label), "move %u%% (%s), per frame", percent, name);
                Say(label, n, ms, "ms");
                FormatTo(label, sizeof(label), "move %u%% (%s), per moved item", percent, name);
                Say(label, n, moving > 0 ? ms * 1000.0 / moving : 0.0, "us");
            }
            // The pair feed over the moved set.
            PairTracker tracker;
            PairEvents events;
            for (u32 i = 0; i < n; ++i)
            {
                tracker.Touch(handles[i]);
            }
            tracker.Update(set, &events);  // the first update creates every pair
            for (u32 i = 0; i < n; ++i)
            {
                Aabbd& b = city.boxes[i];
                const Vec3d d(rng.Float(-0.3f, 0.3f), 0, rng.Float(-0.3f, 0.3f));
                b = Aabbd::FromMinMax(b.min + d, b.max + d);
                set.Move(handles[i], b);
                tracker.Touch(handles[i]);
            }
            const f64 t1 = NowMs();
            tracker.Update(set, &events);
            char label[96];
            FormatTo(label, sizeof(label), "pair update, all touched (%s)", name);
            Say(label, n, NowMs() - t1, "ms");
        }
    }
}

namespace
{

struct BenchRig
{
    ecs::World world;
    ecs::Schedule schedule;
    explicit BenchRig(jobs::Scheduler* scheduler)
    {
        world.SetResource(ecs::TransformReport{});
        world.SetResource(SpatialWorld());
        schedule.Add(ecs::MakeTransformSystem("transforms"));
        schedule.Add(MakeSpatialSyncSystem("spatial_sync").InStage(ecs::kPostUpdate));
        schedule.SetScheduler(scheduler);
        REQUIRE(schedule.Build().ok);
    }
};

}  // namespace

TEST_CASE("Spatial benchmark: 100,000 moving agents through the ECS sync, 1, 2, 4 and 8 workers", "[.][spatial-bench]")
{
    constexpr u32 kAgents = 100000;
    for (usize workers : {usize(1), usize(2), usize(4), usize(8)})
    {
        jobs::Scheduler::Config config;
        config.participants = workers;
        config.pin = false;
        jobs::Scheduler jobs(config);
        BenchRig rig(workers == 1 ? nullptr : &jobs);
        Rng rng(9);
        Vector<ecs::Entity> agents;
        const f64 half = Sqrt(static_cast<f64>(kAgents) * 8.0f) * 0.5f;
        for (u32 i = 0; i < kAgents; ++i)
        {
            ecs::LocalTransform local;
            local.position = Vec3d(rng.Float(-half, half), 0, rng.Float(-half, half));
            SpatialBounds bounds;
            bounds.local = spatial_test::LocalBoxAt(Vec3(0, 0, 0), Vec3(0.5f, 0.5f, 0.5f));
            SpatialMembership membership;
            membership.sets = 3;
            agents.Push(rig.world.Create(local, ecs::WorldTransform{}, bounds, membership));
        }
        rig.schedule.Run(rig.world, 0.016f);
        rig.schedule.Run(rig.world, 0.016f);
        const u32 frames = 5;
        const f64 t0 = NowMs();
        for (u32 fr = 0; fr < frames; ++fr)
        {
            for (u32 i = 0; i < kAgents; ++i)
            {
                ecs::LocalTransform* l = rig.world.Get<ecs::LocalTransform>(agents[i]);
                l->position = l->position + Vec3d(rng.Float(-0.3f, 0.3f), 0, rng.Float(-0.3f, 0.3f));
            }
            rig.schedule.Run(rig.world, 0.016f);
        }
        const f64 per_frame = (NowMs() - t0) / frames;
        char label[96];
        FormatTo(label, sizeof(label), "sync, 100%% moving, %u workers (includes the moves)",
                 static_cast<u32>(workers));
        Say(label, kAgents, per_frame, "ms");
        CHECK(rig.world.Resource<SpatialWorld>().Stats().items_moved > 0);
    }
}

TEST_CASE("Spatial benchmark: the dynamic tree's margin against the cost of moving 100,000 agents a step each",
          "[.][spatial-bench]")
{
    constexpr u32 kAgents = 100000;
    for (f64 margin : {0.0f, 0.25f, 0.5f, 1.0f})
    {
        for (f64 predictive : {0.0f, 2.0f})
        {
            SpatialSetConfig config;
            config.bvh.fat_margin = margin;
            config.bvh.predictive = predictive;
            City city(kAgents, 31);
            SpatialSet set(config);
            Vector<SetHandle> handles;
            for (u32 i = 0; i < kAgents; ++i)
            {
                handles.Push(set.AddDynamic(city.boxes[i], i, 0));
            }
            Rng rng(4);
            Vec3d velocity[1024];
            for (u32 i = 0; i < 1024; ++i)
            {
                velocity[i] = Vec3d(rng.Float(-0.3f, 0.3f), 0, rng.Float(-0.3f, 0.3f));
            }
            const u32 frames = 6;
            f64 t0 = 0;
            for (u32 fr = 0; fr < frames; ++fr)
            {
                if (fr == 1)
                {
                    t0 = NowMs();  // the first frame settles the trees
                }
                for (u32 i = 0; i < kAgents; ++i)
                {
                    Aabbd& b = city.boxes[i];
                    const Vec3d d = velocity[i & 1023];  // agents keep their heading: the predictive margin has
                                                         // something to predict
                    b = Aabbd::FromMinMax(b.min + d, b.max + d);
                    set.Move(handles[i], b);
                }
            }
            char label[96];
            FormatTo(label, sizeof(label), "tree move 100%%, margin %.2f predictive %.0f", static_cast<f64>(margin),
                     static_cast<f64>(predictive));
            Say(label, kAgents, (NowMs() - t0) / (frames - 1), "ms/frame");
        }
    }
}
