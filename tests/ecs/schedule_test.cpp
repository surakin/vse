#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "ecs/schedule.h"
#include "settings/registry.h"
#include "string/utf8.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ecs;

namespace {

struct SPos { float x, y; };
struct SVel { float x, y; };
struct SHealth { u32 hp; };
struct STime { u32 frames = 0; f32 last_dt = 0; };
struct SOther { u32 v = 0; };

Vector<const char*>* g_trace = nullptr;

// A system function that records its name: the name is a literal template argument stand-in (a function per name).
#define TRACE_SYSTEM(fn_name, label) \
    void fn_name(SystemContext&) { g_trace->Push(label); }
TRACE_SYSTEM(TraceA, "a")
TRACE_SYSTEM(TraceB, "b")
TRACE_SYSTEM(TraceC, "c")
TRACE_SYSTEM(TraceD, "d")

bool Contains(const char* text, const char* part) {
    const StringView whole(text);
    const StringView needle(part);
    for (usize at = 0; at + needle.Length() <= whole.Length(); ++at) {
        usize k = 0;
        while (k < needle.Length() && whole[at + k] == needle[k]) {
            ++k;
        }
        if (k == needle.Length()) {
            return true;
        }
    }
    return false;
}

SystemDesc Traced(const char* name, void (*fn)(SystemContext&)) {
    return MakeSystem(name, [fn](SystemContext& c) { fn(c); });
}

} // namespace

TEST_CASE("Schedule: order follows stages, then constraints, then registration", "[ecs][schedule]") {
    Vector<const char*> trace;
    g_trace = &trace;
    Schedule schedule;
    schedule.Add(Traced("d", &TraceD).InStage(kPostUpdate));
    schedule.Add(Traced("c", &TraceC).After("b"));
    schedule.Add(Traced("b", &TraceB));
    schedule.Add(Traced("a", &TraceA).InStage(kPreUpdate));
    const BuildReport report = schedule.Build();
    REQUIRE(report.ok);
    CHECK(StringView(schedule.RunOrderName(0)) == StringView("a"));
    CHECK(StringView(schedule.RunOrderName(1)) == StringView("b"));
    CHECK(StringView(schedule.RunOrderName(2)) == StringView("c"));
    CHECK(StringView(schedule.RunOrderName(3)) == StringView("d"));
    World world;
    schedule.Run(world, 0.016f);
    REQUIRE(trace.Size() == 4);
    CHECK(StringView(trace[0]) == StringView("a"));
    CHECK(StringView(trace[3]) == StringView("d"));
    g_trace = nullptr;
}

TEST_CASE("Schedule: Before and registration order break ties stably", "[ecs][schedule]") {
    Schedule schedule;
    Vector<const char*> trace;
    g_trace = &trace;
    schedule.Add(Traced("a", &TraceA));
    schedule.Add(Traced("b", &TraceB));
    schedule.Add(Traced("c", &TraceC).Before("a"));
    REQUIRE(schedule.Build().ok);
    CHECK(StringView(schedule.RunOrderName(0)) == StringView("b")); // b is free and registered before c? c has no predecessor but comes later: b first
    CHECK(StringView(schedule.RunOrderName(1)) == StringView("c"));
    CHECK(StringView(schedule.RunOrderName(2)) == StringView("a"));
    g_trace = nullptr;
}

TEST_CASE("Schedule: bad constraints are rejected with a message naming the problem", "[ecs][schedule]") {
    {
        Schedule schedule;
        schedule.Add(Traced("a", &TraceA).After("c"));
        schedule.Add(Traced("b", &TraceB).After("a"));
        schedule.Add(Traced("c", &TraceC).After("b"));
        const BuildReport report = schedule.Build();
        CHECK_FALSE(report.ok);
        CHECK(Contains(report.message, "cycle"));
        CHECK(Contains(report.message, "a"));
        CHECK(Contains(report.message, "b"));
        CHECK(Contains(report.message, "c"));
    }
    {
        Schedule schedule;
        schedule.Add(Traced("a", &TraceA).After("nobody"));
        const BuildReport report = schedule.Build();
        CHECK_FALSE(report.ok);
        CHECK(Contains(report.message, "nobody"));
    }
    {
        Schedule schedule;
        schedule.Add(Traced("a", &TraceA));
        schedule.Add(Traced("a", &TraceB));
        CHECK_FALSE(schedule.Build().ok);
    }
    {
        Schedule schedule;
        schedule.Add(Traced("early", &TraceA).InStage(kPreUpdate).After("late"));
        schedule.Add(Traced("late", &TraceB).InStage(kRender));
        const BuildReport report = schedule.Build();
        CHECK_FALSE(report.ok);
        CHECK(Contains(report.message, "stage order"));
    }
}

TEST_CASE("Schedule: a stage added by the program runs where it was put", "[ecs][schedule]") {
    Vector<const char*> trace;
    g_trace = &trace;
    Schedule schedule;
    const StageId physics = schedule.AddStage("physics", kPreUpdate);
    schedule.Add(Traced("a", &TraceA));
    schedule.Add(Traced("b", &TraceB).InStage(physics));
    REQUIRE(schedule.Build().ok);
    World world;
    schedule.Run(world, 0.0f);
    REQUIRE(trace.Size() == 2);
    CHECK(StringView(trace[0]) == StringView("b"));
    g_trace = nullptr;
}

TEST_CASE("Schedule: a failed run condition skips the system", "[ecs][schedule]") {
    Vector<const char*> trace;
    g_trace = &trace;
    Schedule schedule;
    schedule.Add(Traced("a", &TraceA).RunIf([](SystemContext& c) { return c.Frame() % 2 == 0; }));
    REQUIRE(schedule.Build().ok);
    schedule.SetStatsEnabled(true);
    World world;
    for (int i = 0; i < 4; ++i) {
        schedule.Run(world, 0.0f);
    }
    CHECK(trace.Size() == 2);
    CHECK(schedule.StatsOf("a")->skipped_runs == 2);
    CHECK(schedule.StatsOf("a")->runs == 2);
    g_trace = nullptr;
}

TEST_CASE("Schedule: query systems run and their commands apply at the end of the stage", "[ecs][schedule]") {
    World world;
    world.SetResource(STime{});
    for (u32 i = 0; i < 100; ++i) {
        world.Create(SPos{0, 0}, SVel{1, 2}, SHealth{i});
    }
    Schedule schedule;
    schedule.Add(MakeQuerySystem<Write<SPos>, Read<SVel>>("move", [](SystemContext& c, Query<Write<SPos>, Read<SVel>>& q) {
        const f32 dt = c.Dt();
        q.ForEach([dt](SPos& p, const SVel& v) {
            p.x += v.x * dt;
            p.y += v.y * dt;
        });
    }));
    schedule.Add(MakeQuerySystem<Read<SHealth>>("reap", [](SystemContext& c, Query<Read<SHealth>>& q) {
                     q.ForEach([&](Entity e, const SHealth& h) {
                         if (h.hp % 2 == 1) {
                             c.Commands().Destroy(e);
                         }
                     });
                 }).After("move"));
    schedule.Add(MakeSystem("clock", [](SystemContext& c) {
                     STime& t = c.Resource<STime>();
                     ++t.frames;
                     t.last_dt = c.Dt();
                 }).WritesResource<STime>());
    REQUIRE(schedule.Build().ok);
    schedule.Run(world, 0.5f);
    CHECK(world.Query<Read<SHealth>>().Count() == 50);
    CHECK(world.Resource<STime>().frames == 1);
    CHECK(world.Resource<STime>().last_dt == 0.5f);
    float x = 0;
    world.Query<Read<SPos>>().ForEach([&](const SPos& p) { x = p.x; });
    CHECK(x == 0.5f);
    CHECK(schedule.Stats().commands_applied == 50);
    CHECK(schedule.Stats().access_violations == 0);
    CHECK(world.Validate().Ok());
}

TEST_CASE("Schedule: commands of one stage apply in registration order whatever the run order", "[ecs][schedule]") {
    World world;
    const Entity e = world.Create(SHealth{1});
    static Entity target;
    target = e;
    Schedule schedule;
    schedule.Add(MakeSystem("destroyer", [](SystemContext& c) { c.Commands().Destroy(target); }));
    schedule.Add(MakeSystem("adder", [](SystemContext& c) { c.Commands().Add<SPos>(target, SPos{1, 1}); }).Before("destroyer"));
    REQUIRE(schedule.Build().ok);
    CHECK(StringView(schedule.RunOrderName(0)) == StringView("adder")); // runs first...
    schedule.Run(world, 0.0f);
    // ...but "destroyer" was registered first, so its command applies first and the add finds a dead entity.
    CHECK_FALSE(world.Exists(e));
    CHECK(schedule.Stats().commands_applied == 1);
    CHECK(schedule.Stats().commands_ignored == 1);
}

TEST_CASE("Schedule: touching what the system did not declare is counted", "[ecs][schedule]") {
    World world;
    world.SetResource(STime{});
    world.SetResource(SOther{});
    world.Create(SPos{0, 0});
    Schedule schedule;
    schedule.Add(MakeSystem("sloppy", [](SystemContext& c) {
                     auto q = c.Query<Write<SPos>>();    // declared read only: a violation
                     (void)q.Count();
                     c.Resource<SOther>();               // not declared at all: a violation
                     c.ReadResource<STime>();            // declared: fine
                 }).Reads<SPos>().ReadsResource<STime>());
    schedule.Add(MakeSystem("tidy", [](SystemContext& c) {
                     auto q = c.Query<Read<SPos>>();
                     (void)q.Count();
                 }).Reads<SPos>());
    REQUIRE(schedule.Build().ok);
    schedule.Run(world, 0.0f);
    CHECK(schedule.Stats().access_violations == 2);
}

TEST_CASE("Schedule: statistics count visited entities and skipped chunks", "[ecs][schedule]") {
    World world;
    for (u32 i = 0; i < 1000; ++i) {
        world.Create(SPos{0, 0}, SVel{1, 1});
    }
    Schedule schedule;
    schedule.SetStatsEnabled(true);
    schedule.Add(MakeQuerySystem<Write<SPos>, Read<SVel>>("move", [](SystemContext&, Query<Write<SPos>, Read<SVel>>& q) {
        q.ForEach([](SPos& p, const SVel& v) { p.x += v.x; });
    }));
    schedule.Add(MakeQuerySystem<Changed<SVel>, Read<SVel>>("watch", [](SystemContext&, Query<Changed<SVel>, Read<SVel>>& q) {
        q.ForEach([](const SVel&) {});
        q.Finish();
    }));
    REQUIRE(schedule.Build().ok);
    schedule.Run(world, 0.0f);
    CHECK(schedule.StatsOf("move")->entities == 1000);
    CHECK(schedule.StatsOf("move")->chunks_visited >= 1);
    CHECK(schedule.StatsOf("watch")->entities == 1000); // creation counts as a change
    schedule.Run(world, 0.0f);
    CHECK(schedule.StatsOf("watch")->entities == 0);
    CHECK(schedule.StatsOf("watch")->chunks_skipped >= 1);
    CHECK(schedule.StatsOf("move")->runs == 2);
    CHECK(schedule.StatsOf("move")->AverageNanos() > 0);
}

TEST_CASE("Schedule: statistics are off unless asked for", "[ecs][schedule]") {
    World world;
    Schedule schedule;
    schedule.Add(MakeSystem("a", [](SystemContext&) {}));
    REQUIRE(schedule.Build().ok);
    schedule.Run(world, 0.0f);
    CHECK(schedule.StatsOf("a")->runs == 0);
    CHECK(schedule.Stats().frames == 1);
}

TEST_CASE("Schedule: the settings are declared and read at the frame boundary", "[ecs][schedule]") {
    settings::Registry registry;
    RegisterEcsSettings(&registry);
    CHECK(registry.Find(StringView("ecs.stats")).IsValid());
    CHECK(registry.Find(StringView("ecs.parallel_systems")).IsValid());
    Schedule schedule;
    schedule.UseSettings(registry);
    schedule.Add(MakeSystem("a", [](SystemContext&) {}));
    REQUIRE(schedule.Build().ok);
    World world;
    schedule.Run(world, 0.0f);
    CHECK(schedule.StatsOf("a")->runs == 0);
    CHECK(schedule.ParallelRequested());
    registry.Publish(registry.Find(StringView("ecs.stats")), settings::Value::FromBool(true));
    registry.Publish(registry.Find(StringView("ecs.parallel_systems")), settings::Value::FromBool(false));
    schedule.Run(world, 0.0f);
    CHECK(schedule.StatsOf("a")->runs == 1);
    CHECK_FALSE(schedule.ParallelRequested());
}

TEST_CASE("Schedule: a system function too big for the inline buffer is stored on the heap and counted", "[ecs][schedule]") {
    const u64 before = Schedule::HeapFallbacks();
    struct Fat {
        byte pad[256];
        u32* counter;
        void operator()(SystemContext&) { ++*counter; }
    };
    u32 counter = 0;
    Schedule schedule;
    schedule.Add(MakeSystem("fat", Fat{{}, &counter}));
    CHECK(Schedule::HeapFallbacks() == before + 1);
    REQUIRE(schedule.Build().ok);
    World world;
    schedule.Run(world, 0.0f);
    schedule.Run(world, 0.0f);
    CHECK(counter == 2);
}

TEST_CASE("Schedule: a ten-system pipeline over 100,000 entities stays valid for 100 frames", "[ecs][schedule]") {
    World world;
    for (u32 i = 0; i < 100000; ++i) {
        if (i % 3 == 0) {
            world.Create(SPos{0, 0}, SVel{1, 1}, SHealth{i});
        } else {
            world.Create(SPos{0, 0}, SVel{1, 1});
        }
    }
    Schedule schedule;
    static const char* const names[10] = {"s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9"};
    for (u32 i = 0; i < 10; ++i) {
        const bool reaper = i == 9;
        if (reaper) {
            schedule.Add(MakeQuerySystem<Read<SHealth>>(names[i], [](SystemContext& c, Query<Read<SHealth>>& q) {
                q.ForEach([&](Entity e, const SHealth& h) {
                    if (h.hp % 9 == 0) {
                        c.Commands().Destroy(e);
                    }
                });
            }));
        } else {
            schedule.Add(MakeQuerySystem<Write<SPos>, Read<SVel>>(names[i], [](SystemContext& c, Query<Write<SPos>, Read<SVel>>& q) {
                const f32 dt = c.Dt();
                q.ForEach([dt](SPos& p, const SVel& v) { p.x += v.x * dt; });
            }));
        }
    }
    REQUIRE(schedule.Build().ok);
    for (u32 frame = 0; frame < 100; ++frame) {
        schedule.Run(world, 0.01f);
        if (frame % 10 == 9) {
            REQUIRE(world.Validate().Ok());
        }
    }
    CHECK(schedule.Stats().frames == 100);
    CHECK(schedule.Stats().access_violations == 0);
    u32 reaped = 0;
    world.Query<Read<SPos>, Without<SHealth>>().ForEach([&](const SPos&) { ++reaped; });
    CHECK(reaped == 66666);
}

TEST_CASE("Schedule: a query system follows the schedule to another world", "[ecs][schedule]") {
    Schedule schedule;
    u32 seen = 0;
    static u32* sink;
    sink = &seen;
    schedule.Add(MakeQuerySystem<Read<SPos>>("count", [](SystemContext&, Query<Read<SPos>>& q) { *sink = q.Count(); }));
    REQUIRE(schedule.Build().ok);
    World first; // both worlds stay alive: the query must not simply be reading the first one
    for (u32 i = 0; i < 5; ++i) {
        first.Create(SPos{0, 0});
    }
    schedule.Run(first, 0.0f);
    CHECK(seen == 5);
    World second;
    for (u32 i = 0; i < 7; ++i) {
        second.Create(SVel{0, 0}, SPos{0, 0});
    }
    schedule.Run(second, 0.0f);
    CHECK(seen == 7);
}

TEST_CASE("Schedule: statistics turned on later report one run, not the run-up", "[ecs][schedule]") {
    World world;
    for (u32 i = 0; i < 1000; ++i) {
        world.Create(SPos{0, 0});
    }
    Schedule schedule;
    schedule.Add(MakeQuerySystem<Read<SPos>>("scan", [](SystemContext&, Query<Read<SPos>>& q) { q.ForEach([](const SPos&) {}); }));
    REQUIRE(schedule.Build().ok);
    schedule.Run(world, 0.0f);
    schedule.Run(world, 0.0f);
    schedule.SetStatsEnabled(true);
    schedule.Run(world, 0.0f);
    CHECK(schedule.StatsOf("scan")->entities == 1000);
}
