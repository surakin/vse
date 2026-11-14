// spatial_demo: Phase 18 Day 248. 50,000 entities (static city blocks and moving agents) in an ECS world with the
// spatial sync, printing what the spatial module answers each frame: the entities a camera culls, the contacts of the
// physics set, the entity under a ray, the neighbors within a radius, and optionally the statistics and the size of the
// debug drawing. Headless: it draws nothing (the lines are counted, a program with a line pass would draw them).
//
//   spatial_demo [--entities N]  entity count (default 50000; a fifth are static blocks)
//                [--frames N]    frames to run (default 120)
//                [--stats]       print the statistics of the render set every 30 frames
//                [--debug-draw]  also produce the debug lines of the render set and print their count
//                [--serial]      compute the world boxes on one thread
#include "containers/vector.h"
#include "cstr/cstr.h"
#include "cstr/number.h"
#include "debug/program_setup.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "ecs/world.h"
#include "jobs/scheduler.h"
#include "lang/scalar.h"
#include "math/geometry.h"
#include "os/console.h"
#include "os/os.h"
#include "spatial/components.h"
#include "spatial/entity_query.h"
#include "spatial/query.h"
#include "spatial/spatial_debug.h"
#include "spatial/spatial_sync.h"
#include "spatial/spatial_world.h"

using namespace engine_placeholder;
using namespace engine_placeholder::spatial;

namespace
{

struct Rng
{
    u64 state = 0x9E3779B97F4A7C15ull;
    u32 Next()
    {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<u32>(state >> 33);
    }
    f32 Range(f32 lo, f32 hi)
    {
        return lo + (hi - lo) * (static_cast<f32>(Next() & 0xFFFFFF) / static_cast<f32>(1u << 24));
    }
};

Aabb BoxOf(const Vec3& c, const Vec3& h)
{
    return Aabb::FromMinMax(c - h, c + h);
}

}  // namespace

int main(int argc, char** argv)
{
    const debug::ProgramDebugSetup debug_setup = debug::StartProgramDebugging(argc, argv);
    debug::RunCrashTest(debug_setup.crash_test);
    u32 entities = 50000;
    u32 frames = 120;
    bool stats = false;
    bool debug_draw = false;
    bool serial = false;
    static const os::CommandLineOption kCliOptions[] = {
        {"entities", true}, {"frames", true},           {"stats", false},       {"debug-draw", false},
        {"serial", false},  debug::kCrashHandlerOption, debug::kCrashTestOption};
    const os::CommandLine<HeapAllocator> line(HeapAllocator::Default(), argc, argv,
                                              Span<const os::CommandLineOption>(kCliOptions, 7));
    const char* program = line.ProgramName().Data();
    if (!line.Ok() || line.ArgCount() != 0)
    {
        os::PrintError("usage: %s [--entities N] [--frames N] [--stats] [--debug-draw] [--serial]\n", program);
        return 2;
    }
    StringView value;
    if (line.TryGetOption("entities", value))
    {
        entities = static_cast<u32>(CStrToLong(value.Data()));
    }
    if (line.TryGetOption("frames", value))
    {
        frames = static_cast<u32>(CStrToLong(value.Data()));
    }
    stats = line.HasFlag("stats");
    debug_draw = line.HasFlag("debug-draw");
    serial = line.HasFlag("serial");
    if (entities == 0 || entities > 5000000)
    {
        entities = 50000;  // a negative or absurd count wrapped to a huge u32
    }
    if (frames > 100000000)
    {
        frames = 120;
    }

    jobs::Scheduler jobs(jobs::Scheduler::Config{});
    ecs::World world;
    ecs::Schedule schedule;
    world.SetResource(ecs::TransformReport{});
    world.SetResource(SpatialWorld());
    world.Resource<SpatialWorld>().SetParallel(!serial);
    schedule.Add(ecs::MakeTransformSystem("transforms"));
    schedule.Add(MakeSpatialSyncSystem("spatial_sync").InStage(ecs::kPostUpdate));
    schedule.SetScheduler(&jobs);
    if (!schedule.Build().ok)
    {
        os::PrintError("spatial_demo: the schedule did not build\n");
        return 1;
    }

    // A city whose density is fixed: about one entity per 40 square units.
    Rng rng;
    const f32 half = Sqrt(static_cast<f32>(entities) * 40.0f) * 0.5f;
    Vector<ecs::Entity> agents;
    for (u32 i = 0; i < entities; ++i)
    {
        const bool block = i % 5 == 0;
        ecs::LocalTransform local;
        local.position = Vec3(rng.Range(-half, half), block ? 4.0f : 0.5f, rng.Range(-half, half));
        SpatialBounds bounds;
        bounds.local = block ? BoxOf(Vec3(0, 0, 0), Vec3(3, 4, 3)) : BoxOf(Vec3(0, 0, 0), Vec3(0.4f, 0.5f, 0.4f));
        SpatialMembership membership;
        membership.sets = 3;  // render and physics
        const ecs::Entity e = world.Create(local, ecs::WorldTransform{}, bounds, membership);
        if (block)
        {
            world.Add<SpatialStatic>(e, SpatialStatic{});
        }
        else
        {
            agents.Push(e);
        }
    }
    os::Print("spatial_demo: %u entities (%u moving agents), a city of %.0f x %.0f units\n", entities,
              static_cast<u32>(agents.Size()), static_cast<double>(half * 2), static_cast<double>(half * 2));

    // The camera: a pyramid at the edge of the city looking across it, seeing about a tenth of it.
    Frustum view;
    {
        const Vec3 apex(0, 3.0f, half);
        const f32 reach = half * 0.7f;
        const f32 w = 0.45f;
        const f32 inv = 1.0f / Sqrt(1.0f + w * w);
        view.planes[0] = Plane::FromPointNormal(apex, Vec3(inv, 0, -w * inv));
        view.planes[1] = Plane::FromPointNormal(apex, Vec3(-inv, 0, -w * inv));
        view.planes[2] = Plane::FromPointNormal(apex - Vec3(0, 50, 0), Vec3(0, 1, 0));
        view.planes[3] = Plane::FromPointNormal(apex + Vec3(0, 50, 0), Vec3(0, -1, 0));
        view.planes[4] = Plane::FromPointNormal(apex + Vec3(0, 0, -0.1f), Vec3(0, 0, -1));
        view.planes[5] = Plane::FromPointNormal(apex + Vec3(0, 0, -reach), Vec3(0, 0, 1));
    }

    Vector<ecs::Entity> neighbors;
    Vector<DebugLine> lines;
    u64 total_contacts = 0;
    u64 picks = 0;
    for (u32 frame = 0; frame < frames; ++frame)
    {
        for (usize i = 0; i < agents.Size(); ++i)
        {
            ecs::LocalTransform* l = world.Get<ecs::LocalTransform>(agents[i]);
            l->position = l->position + Vec3(rng.Range(-0.1f, 0.1f), 0, rng.Range(-0.1f, 0.1f));
        }
        schedule.Run(world, 0.016f);
        SpatialWorld& spatial = world.Resource<SpatialWorld>();
        const SpatialSet& render = *spatial.Set(kRenderSet);

        u32 visible = 0;
        Overlap(render, view, QueryFilter(),
                [&](const Item&)
                {
                    ++visible;
                    return true;
                });

        u32 contacts = 0;
        ForEachPair(*spatial.Set(kPhysicsSet), 0xFFFFFFFFu, 0xFFFFFFFFu, QueryFilter(),
                    [&](const Item&, const Item&)
                    {
                        ++contacts;
                        return true;
                    });
        total_contacts += contacts;

        // A "mouse ray" that sweeps across the city; the entity under it and its neighbors within a radius.
        const f32 sweep = -half + (2.0f * half) * (static_cast<f32>(frame % 60) / 60.0f);
        const Ray ray = Ray::Make(Vec3(sweep, 3.0f, half), Vec3(0, 0, -1));
        EntityHit hit;
        u32 near_count = 0;
        const bool picked = RaycastEntity(spatial, kRenderSet, ray, half * 2.0f, QueryFilter(), &hit);
        if (picked)
        {
            ++picks;
            near_count = static_cast<u32>(
                OverlapEntities(spatial, kRenderSet, BoxOf(hit.point, Vec3(5, 5, 5)), QueryFilter(), &neighbors));
        }

        if (frame % 30 == 0)
        {
            os::Print("frame %3u  visible %6u  contacts %6u  pick %s  neighbors %u\n", frame, visible, contacts,
                      picked ? "hit " : "none", near_count);
            if (stats)
            {
                const SpatialStats s = DescribeSet(render);
                os::Print(
                    "          render set: %u static + %u dynamic items, static tree %u nodes depth %u, dynamic tree %u nodes height %u, sah %.2f (built %.2f), moves %llu (%llu reinserted)\n",
                    s.set.static_items, s.set.dynamic_items, s.static_nodes, s.static_depth, s.dynamic_nodes,
                    s.dynamic_height, static_cast<double>(s.dynamic_sah_cost),
                    static_cast<double>(s.dynamic_built_sah_cost), static_cast<unsigned long long>(s.moves),
                    static_cast<unsigned long long>(s.reinserts));
            }
            if (debug_draw)
            {
                DebugOptions options;
                options.has_query = true;
                options.query = BoxOf(Vec3(sweep, 3, 0), Vec3(1, 1, 1));
                os::Print("          debug lines: %u\n", static_cast<u32>(DebugLines(render, options, &lines)));
            }
        }
    }
    const SpatialWorldStats& ws = world.Resource<SpatialWorld>().Stats();
    os::Print("spatial_demo: %u frames, %llu items moved (%llu reinserted), %llu contacts in all, %llu picks\n", frames,
              static_cast<unsigned long long>(ws.items_moved), static_cast<unsigned long long>(ws.items_reinserted),
              static_cast<unsigned long long>(total_contacts), static_cast<unsigned long long>(picks));
    os::Print("spatial_demo: ok\n");
    return 0;
}
