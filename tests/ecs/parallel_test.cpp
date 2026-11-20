#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "log/output.h"
#include "cstr/mem_ops.h"
#include "ecs/schedule.h"
#include "jobs/scheduler.h"
#include "os/console.h"
#include "os/os.h"
#include "test_rng.h"

using namespace vse;
using namespace vse::ecs;

namespace
{

struct PA
{
    float x, y;
};
struct PB
{
    float x, y;
};
struct PC
{
    u32 v;
};
struct PD
{
    u32 v;
};
struct PE
{
    float v;
};
struct PMark
{
    u32 value;
};
struct PSum
{
    double total = 0;
};

jobs::Scheduler::Config ConfigFor(usize participants)
{
    jobs::Scheduler::Config config;
    config.participants = participants;
    config.pin = false;
    return config;
}

// FNV-1a over every component value of every entity, in iteration order: two worlds built and run the same way hash
// equal.
u64 HashWorld(World& world)
{
    u64 h = 1469598103934665603ull;
    auto mix = [&](const void* data, usize size)
    {
        const byte* bytes = static_cast<const byte*>(data);
        for (usize i = 0; i < size; ++i)
        {
            h = (h ^ bytes[i]) * 1099511628211ull;
        }
    };
    world.Query<Read<PA>, Read<PB>, Read<PC>, Read<PD>, Read<PE>>().ForEach(
        [&](const PA& a, const PB& b, const PC& c, const PD& d, const PE& e)
        {
            mix(&a, sizeof(a));
            mix(&b, sizeof(b));
            mix(&c, sizeof(c));
            mix(&d, sizeof(d));
            mix(&e, sizeof(e));
        });
    return h;
}

void FillWorld(World& world, u32 count)
{
    for (u32 i = 0; i < count; ++i)
    {
        world.Create(PA{static_cast<float>(i), 1.0f}, PB{0.5f, static_cast<float>(i % 7)}, PC{i}, PD{i * 3},
                     PE{static_cast<float>(i) * 0.25f});
    }
    world.SetResource(PSum{});
}

// Eight systems with a mix of disjoint writers, readers after writers and one system alone in its wave that splits its
// chunks.
void AddPipeline(Schedule& schedule)
{
    schedule.Add(MakeQuerySystem<Write<PA>, Read<PB>>("integrate",
                                                      [](SystemContext& c, Query<Write<PA>, Read<PB>>& q)
                                                      {
                                                          const f32 dt = c.Dt();
                                                          c.ForEachChunkParallel(q,
                                                                                 [dt](ChunkView& view, CommandBuffer&)
                                                                                 {
                                                                                     Span<PA> a = view.Write<PA>();
                                                                                     Span<const PB> b = view.Read<PB>();
                                                                                     for (usize i = 0; i < a.Size();
                                                                                          ++i)
                                                                                     {
                                                                                         a[i].x += b[i].x * dt;
                                                                                         a[i].y += b[i].y * dt;
                                                                                     }
                                                                                 });
                                                      }));
    schedule.Add(MakeQuerySystem<Write<PC>>("scramble", [](SystemContext&, Query<Write<PC>>& q)
                                            { q.ForEach([](PC& c) { c.v = c.v * 3u + 1u; }); }));
    schedule.Add(MakeQuerySystem<Write<PE>>("decay", [](SystemContext&, Query<Write<PE>>& q)
                                            { q.ForEach([](PE& e) { e.v *= 0.99f; }); }));
    schedule.Add(MakeQuerySystem<Write<PD>, Read<PC>>("fold", [](SystemContext&, Query<Write<PD>, Read<PC>>& q)
                                                      { q.ForEach([](PD& d, const PC& c) { d.v += c.v & 0xFFu; }); }));
    schedule.Add(
        MakeQuerySystem<Write<PB>, Read<PA>>("steer", [](SystemContext&, Query<Write<PB>, Read<PA>>& q)
                                             { q.ForEach([](PB& b, const PA& a) { b.y = a.y * 0.5f + b.x; }); }));
    schedule.Add(MakeQuerySystem<Read<PD>, Read<PE>>("measure",
                                                     [](SystemContext& c, Query<Read<PD>, Read<PE>>& q)
                                                     {
                                                         const double total = c.ReduceChunks<double>(
                                                             q, 0.0,
                                                             [](ChunkView& view)
                                                             {
                                                                 double s = 0;
                                                                 Span<const PD> d = view.Read<PD>();
                                                                 Span<const PE> e = view.Read<PE>();
                                                                 for (usize i = 0; i < d.Size(); ++i)
                                                                 {
                                                                     s += static_cast<double>(d[i].v) * 1e-3 +
                                                                          static_cast<double>(e[i].v);
                                                                 }
                                                                 return s;
                                                             },
                                                             [](double acc, double part) { return acc + part; });
                                                         c.Resource<PSum>().total = total;
                                                     })
                     .WritesResource<PSum>());
}

}  // namespace

TEST_CASE("Parallel: waves follow the declared access and keep the sequential order", "[ecs][parallel]")
{
    Schedule schedule;
    auto nop = [](SystemContext&) {};
    schedule.Add(MakeSystem("w_a", nop).Writes<PA>());
    schedule.Add(MakeSystem("w_b", nop).Writes<PB>());
    schedule.Add(MakeSystem("r_a", nop).Reads<PA>());
    schedule.Add(MakeSystem("r_a2", nop).Reads<PA>());
    schedule.Add(MakeSystem("w_a2", nop).Writes<PA>());
    schedule.Add(MakeSystem("res_w", nop).WritesResource<PSum>());
    schedule.Add(MakeSystem("res_r", nop).ReadsResource<PSum>());
    schedule.Add(MakeSystem("free", nop));
    REQUIRE(schedule.Build().ok);
    CHECK(schedule.WaveOf(0) == 0);  // w_a
    CHECK(schedule.WaveOf(1) == 0);  // w_b: disjoint writer shares the wave
    CHECK(schedule.WaveOf(2) == 1);  // r_a reads what w_a writes
    CHECK(schedule.WaveOf(3) == 1);  // two readers share a wave
    CHECK(schedule.WaveOf(4) == 2);  // w_a2 conflicts with the readers before it
    CHECK(schedule.WaveOf(5) == 0);  // resource writer, nothing conflicts yet
    CHECK(schedule.WaveOf(6) == 1);  // reader of the resource after its writer
    CHECK(schedule.WaveOf(7) == 0);  // declares nothing
}

TEST_CASE("Parallel: an ordering constraint puts a system in a later wave", "[ecs][parallel]")
{
    Schedule schedule;
    auto nop = [](SystemContext&) {};
    schedule.Add(MakeSystem("first", nop).Writes<PA>());
    schedule.Add(MakeSystem("second", nop).Writes<PB>().After("first"));
    REQUIRE(schedule.Build().ok);
    CHECK(schedule.WaveOf(0) == 0);
    CHECK(schedule.WaveOf(1) == 1);
}

TEST_CASE("Parallel: a frame equals the sequential frame bit for bit, at every worker count", "[ecs][parallel]")
{
    World reference;
    FillWorld(reference, 20000);
    {
        Schedule sequential;
        AddPipeline(sequential);
        REQUIRE(sequential.Build().ok);
        for (u32 frame = 0; frame < 10; ++frame)
        {
            sequential.Run(reference, 0.016f);
        }
    }
    const u64 expected = HashWorld(reference);
    const double expected_sum = reference.Resource<PSum>().total;
    for (usize participants = 1; participants <= 8; ++participants)
    {
        for (int repeat = 0; repeat < 3; ++repeat)
        {
            jobs::Scheduler jobs(ConfigFor(participants));
            World world;
            FillWorld(world, 20000);
            Schedule schedule;
            schedule.SetScheduler(&jobs);
            AddPipeline(schedule);
            REQUIRE(schedule.Build().ok);
            for (u32 frame = 0; frame < 10; ++frame)
            {
                schedule.Run(world, 0.016f);
            }
            CHECK(HashWorld(world) == expected);
            CHECK(world.Resource<PSum>().total == expected_sum);  // exact, not approximately
            CHECK(schedule.Stats().access_violations == 0);
            CHECK(world.Validate().Ok());
            if (participants > 1)
            {
                CHECK(jobs.TotalResumes() > 0);  // the waves and the chunk ranges really ran as jobs
            }
        }
    }
}

TEST_CASE("Parallel: 200,000 entities over 50 frames equal the sequential run", "[ecs][parallel]")
{
    World reference;
    FillWorld(reference, 200000);
    {
        Schedule sequential;
        AddPipeline(sequential);
        REQUIRE(sequential.Build().ok);
        for (u32 frame = 0; frame < 50; ++frame)
        {
            sequential.Run(reference, 0.016f);
        }
    }
    jobs::Scheduler jobs(ConfigFor(4));
    World world;
    FillWorld(world, 200000);
    Schedule schedule;
    schedule.SetScheduler(&jobs);
    AddPipeline(schedule);
    REQUIRE(schedule.Build().ok);
    for (u32 frame = 0; frame < 50; ++frame)
    {
        schedule.Run(world, 0.016f);
    }
    CHECK(HashWorld(world) == HashWorld(reference));
}

TEST_CASE("Parallel: turning the setting off runs the same frame on the calling thread", "[ecs][parallel]")
{
    settings::Registry registry;
    RegisterEcsSettings(&registry);
    registry.Publish(registry.Find(StringView("ecs.parallel_systems")), settings::Value::FromBool(false));
    jobs::Scheduler jobs(ConfigFor(4));
    World world;
    FillWorld(world, 5000);
    World reference;
    FillWorld(reference, 5000);
    Schedule schedule;
    schedule.SetScheduler(&jobs);
    schedule.UseSettings(registry);
    AddPipeline(schedule);
    Schedule sequential;
    AddPipeline(sequential);
    REQUIRE(schedule.Build().ok);
    REQUIRE(sequential.Build().ok);
    for (u32 frame = 0; frame < 5; ++frame)
    {
        schedule.Run(world, 0.016f);
        sequential.Run(reference, 0.016f);
    }
    CHECK_FALSE(schedule.ParallelRequested());
    CHECK(HashWorld(world) == HashWorld(reference));
    for (usize p = 0; p < jobs.ParticipantCount(); ++p)
    {
        CHECK(jobs.ResumesBy(p) == 0);  // nothing was submitted
    }
}

TEST_CASE("Parallel: ReduceChunks gives the sequential floating-point sum at any worker count", "[ecs][parallel]")
{
    World world;
    ecs_test::Rng rng(7);
    for (u32 i = 0; i < 50000; ++i)
    {
        // values of very different magnitudes make the sum depend on the order of addition
        const float v = static_cast<float>(rng.Below(1000000)) * (i % 5 == 0 ? 1e-6f : 1e3f);
        world.Create(PE{v});
    }
    auto q = world.Query<Read<PE>>();
    auto per_chunk = [](ChunkView& view)
    {
        float s = 0;
        for (const PE& e : view.Read<PE>())
        {
            s += e.v;
        }
        return s;
    };
    auto combine = [](float a, float b) { return a + b; };
    const float inline_sum = q.ReduceChunks<float>(nullptr, 0.0f, per_chunk, combine, 4);
    for (usize participants = 1; participants <= 6; ++participants)
    {
        jobs::Scheduler jobs(ConfigFor(participants));
        const float sum = q.ReduceChunks<float>(&jobs, 0.0f, per_chunk, combine, 4);
        CHECK(sum == inline_sum);
    }
}

TEST_CASE("Parallel: commands recorded by parallel chunks apply in chunk order", "[ecs][parallel]")
{
    for (usize participants = 1; participants <= 6; ++participants)
    {
        jobs::Scheduler jobs(ConfigFor(participants));
        World world;
        for (u32 i = 0; i < 30000; ++i)
        {
            world.Create(PC{i});
        }
        auto q = world.Query<Read<PC>>();
        const usize chunks = q.ChunkCountUpperBound();
        const usize grain = 3;
        CommandBufferPool pool(static_cast<u32>(Query<Read<PC>>::RangesFor(chunks, grain)));
        q.ForEachChunkParallel(
            &jobs, pool,
            [](ChunkView& view, CommandBuffer& commands)
            {
                commands.Create(PMark{view.Read<PC>()[0].v});  // the value of the chunk's first row
            },
            grain);
        const PlaybackReport report = pool.ApplyAll(world);
        CHECK(report.created == chunks);
        u32 previous = 0;
        u32 seen = 0;
        bool ordered = true;
        world.Query<Read<PMark>>().ForEach(
            [&](const PMark& m)
            {
                ordered = ordered && (seen == 0 || m.value > previous);
                previous = m.value;
                ++seen;
            });
        CHECK(seen == chunks);
        CHECK(ordered);
    }
}

TEST_CASE("Parallel: a wrongly declared system is caught while running in a wave", "[ecs][parallel]")
{
    jobs::Scheduler jobs(ConfigFor(3));
    World world;
    world.Create(PA{0, 0}, PB{0, 0});
    Schedule schedule;
    schedule.SetScheduler(&jobs);
    schedule.Add(MakeSystem("liar",
                            [](SystemContext& c)
                            {
                                auto q = c.Query<Write<PB>>();  // declares PA only
                                (void)q.Count();
                            })
                     .Writes<PA>());
    schedule.Add(MakeSystem("honest",
                            [](SystemContext& c)
                            {
                                auto q = c.Query<Write<PB>>();
                                (void)q.Count();
                            })
                     .Writes<PB>()
                     .Reads<PC>());
    REQUIRE(schedule.Build().ok);
    CHECK(schedule.WaveOf(0) == 0);
    CHECK(schedule.WaveOf(1) == 0);
    schedule.Run(world, 0.0f);
    CHECK(schedule.Stats().access_violations == 1);
}

TEST_CASE("Parallel: 1,000 frames of 16 systems with random structural commands end valid", "[ecs][parallel]")
{
    jobs::Scheduler jobs(ConfigFor(4));
    World world;
    for (u32 i = 0; i < 2000; ++i)
    {
        world.Create(PC{i}, PD{i});
    }
    Schedule schedule;
    schedule.SetScheduler(&jobs);
    static const char* const names[16] = {"t0", "t1", "t2",  "t3",  "t4",  "t5",  "t6",  "t7",
                                          "t8", "t9", "t10", "t11", "t12", "t13", "t14", "t15"};
    for (u32 i = 0; i < 16; ++i)
    {
        switch (i % 4)
        {
        case 0:
            schedule.Add(MakeQuerySystem<Write<PC>>(names[i], [](SystemContext&, Query<Write<PC>>& q)
                                                    { q.ForEach([](PC& c) { c.v += 1; }); }));
            break;
        case 1:
            schedule.Add(MakeQuerySystem<Read<PD>>(names[i],
                                                   [](SystemContext& c, Query<Read<PD>>& q)
                                                   {
                                                       q.ForEach(
                                                           [&](Entity e, const PD& d)
                                                           {
                                                               if ((d.v + static_cast<u32>(c.Frame())) % 97 == 0)
                                                               {
                                                                   c.Commands().Add<PE>(e, PE{1.0f});
                                                               }
                                                           });
                                                   }));
            break;
        case 2:
            schedule.Add(MakeQuerySystem<Read<PE>>(names[i],
                                                   [](SystemContext& c, Query<Read<PE>>& q)
                                                   {
                                                       q.ForEach(
                                                           [&](Entity e, const PE&)
                                                           {
                                                               if (e.index % 3 == c.Frame() % 3)
                                                               {
                                                                   c.Commands().Remove<PE>(e);
                                                               }
                                                           });
                                                   }));
            break;
        default:
            schedule.Add(MakeQuerySystem<Read<PC>>(names[i],
                                                   [](SystemContext& c, Query<Read<PC>>& q)
                                                   {
                                                       q.ForEach(
                                                           [&](Entity e, const PC& v)
                                                           {
                                                               if (v.v % 1009 == 0 && c.Frame() % 5 == 0)
                                                               {
                                                                   c.Commands().Create(PC{v.v + 1}, PD{1});
                                                                   c.Commands().Destroy(e);
                                                               }
                                                           });
                                                   }));
            break;
        }
    }
    REQUIRE(schedule.Build().ok);
    for (u32 frame = 0; frame < 1000; ++frame)
    {
        schedule.Run(world, 0.001f);
        if (frame % 100 == 99)
        {
            REQUIRE(world.Validate().Ok());
        }
    }
    CHECK(schedule.Stats().access_violations == 0);
    CHECK(world.Validate().Ok());
}

TEST_CASE("Parallel: scaling of one heavy system (recorded, not asserted)", "[ecs][parallel][.scaling]")
{
    World world;
    for (u32 i = 0; i < 500000; ++i)
    {
        world.Create(PA{static_cast<float>(i), 1.0f}, PB{0.5f, 2.0f});
    }
    for (usize participants : {usize{1}, usize{2}, usize{4}, usize{8}})
    {
        jobs::Scheduler jobs(ConfigFor(participants));
        Schedule schedule;
        schedule.SetScheduler(&jobs);
        schedule.SetStatsEnabled(true);
        schedule.Add(MakeQuerySystem<Write<PA>, Read<PB>>("heavy",
                                                          [](SystemContext& c, Query<Write<PA>, Read<PB>>& q)
                                                          {
                                                              c.ForEachChunkParallel(
                                                                  q,
                                                                  [](ChunkView& view, CommandBuffer&)
                                                                  {
                                                                      Span<PA> a = view.Write<PA>();
                                                                      Span<const PB> b = view.Read<PB>();
                                                                      for (int round = 0; round < 40; ++round)
                                                                      {
                                                                          for (usize i = 0; i < a.Size(); ++i)
                                                                          {
                                                                              a[i].x = a[i].x * 0.999f + b[i].x;
                                                                              a[i].y = a[i].y * 0.999f + b[i].y;
                                                                          }
                                                                      }
                                                                  });
                                                          }));
        REQUIRE(schedule.Build().ok);
        schedule.Run(world, 0.0f);
        u64 best = ~u64{0};
        for (int frame = 0; frame < 5; ++frame)
        {
            schedule.Run(world, 0.0f);
            const u64 t = schedule.StatsOf("heavy")->last_ns;
            best = t < best ? t : best;
        }
        out::Print("scaling: %zu participants: %.2f ms\n", participants, static_cast<double>(best) / 1e6);
    }
}
