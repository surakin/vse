// The script demo (Phase 27 Day 475): the whole game (a crowd with Lua behaviors, a player, pickups, a planner that
// searches a field with task.parallel_for) run for a few hundred frames at several thread counts, with the same state
// hash each time, and hot reload of its script in the middle of a run.
#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"
#include "demo_world.h"
#include "jobs/scheduler.h"

using namespace engine_placeholder;
using namespace script_demo;

namespace
{

jobs::Scheduler::Config ConfigFor(usize participants)
{
    jobs::Scheduler::Config config;
    config.participants = participants;
    config.pin = false;
    return config;
}

DemoOptions SmallOptions()
{
    DemoOptions options;
    options.entities = 300;
    options.partitions = 16;
    options.field_cells = 4096;
    return options;
}

struct Result
{
    u64 hash = 0;
    u64 pickups = 0;
    u64 errors = 0;
    u64 updates = 0;
    Vec3 marker;
};

Result Run(usize threads, u32 frames)
{
    jobs::Scheduler scheduler(ConfigFor(threads));
    DemoWorld world(SmallOptions());
    for (u32 f = 0; f < frames; ++f)
    {
        world.Frame(&scheduler);
    }
    Result r;
    r.hash = world.StateHash();
    r.pickups = world.PickupsCollected();
    r.errors = world.ScriptStats().errors;
    r.updates = world.ScriptStats().updates;
    r.marker = world.MarkerPosition();
    return r;
}

// The script with the wander force changed.
String<HeapAllocator> Edited(const char* from, const char* to)
{
    String<HeapAllocator> text(HeapAllocator::Default(), StringView(BuiltInScript()));
    const StringView whole = text.View();
    const StringView needle(from, CStrLength(from));
    usize at = whole.Length();
    for (usize i = 0; i + needle.Length() <= whole.Length(); ++i)
    {
        if (whole.Substring(i, needle.Length()) == needle)
        {
            at = i;
            break;
        }
    }
    REQUIRE(at != whole.Length());
    String<HeapAllocator> out(HeapAllocator::Default(), whole.Substring(0, at));
    out.Append(StringView(to, CStrLength(to)));
    out.Append(whole.Substring(at + needle.Length(), whole.Length() - at - needle.Length()));
    return out;
}

Result RunWithReload(usize threads, const String<HeapAllocator>& text, bool* applied)
{
    jobs::Scheduler scheduler(ConfigFor(threads));
    DemoWorld world(SmallOptions());
    for (u32 f = 0; f < 40; ++f)
    {
        world.Frame(&scheduler);
    }
    *applied = world.Reload(text.View());
    for (u32 f = 0; f < 40; ++f)
    {
        world.Frame(&scheduler);
    }
    Result r;
    r.hash = world.StateHash();
    r.errors = world.ScriptStats().errors;
    r.updates = world.ScriptStats().updates;
    return r;
}

}  // namespace

TEST_CASE("Script demo: the game runs, and its state hash is the same at 1, 2 and 16 threads",
          "[script_demo][parallel]")
{
    const Result one = Run(1, 150);
    CHECK(one.hash != 0);
    CHECK(one.errors == 0);
    CHECK(one.updates > 150u * 300u);  // every body ran its behavior every frame
    for (usize threads : {usize{2}, usize{16}})
    {
        INFO("threads " << threads);
        const Result r = Run(threads, 150);
        CHECK(r.hash == one.hash);
        CHECK(r.pickups == one.pickups);
        CHECK(r.errors == 0);
    }
}

TEST_CASE("Script demo: the planner's parallel search puts the marker in the world", "[script_demo][tasks]")
{
    const Result r = Run(4, 60);
    CHECK(r.errors == 0);
    CHECK(r.marker.y == 1.5f);  // the planner set it; it starts at the origin
    CHECK((r.marker.x != 0.0f || r.marker.z != 0.0f));
    // The same cells at one thread: the answer does not depend on the thread count.
    const Result one = Run(1, 60);
    CHECK(one.marker.x == r.marker.x);
    CHECK(one.marker.z == r.marker.z);
}

TEST_CASE("Script demo: the crowd reaches the pickups", "[script_demo]")
{
    const Result r = Run(4, 240);
    CHECK(r.errors == 0);
    CHECK(r.pickups > 0);
}

TEST_CASE("Script demo: hot reload changes the running game, and the result is the same at any thread count",
          "[script_demo][reload]")
{
    const String<HeapAllocator> stronger = Edited("d * 6.0", "d * 9.0");
    bool applied_one = false;
    bool applied_many = false;
    const Result one = RunWithReload(1, stronger, &applied_one);
    const Result many = RunWithReload(16, stronger, &applied_many);
    CHECK(applied_one);
    CHECK(applied_many);
    CHECK(one.errors == 0);
    CHECK(one.hash == many.hash);
    // Without the edit the run is different: the new text ran.
    jobs::Scheduler scheduler(ConfigFor(1));
    DemoWorld plain(SmallOptions());
    for (u32 f = 0; f < 80; ++f)
    {
        plain.Frame(&scheduler);
    }
    CHECK(plain.StateHash() != one.hash);
}

TEST_CASE("Script demo: a script that does not compile is refused and the old one keeps running",
          "[script_demo][reload]")
{
    jobs::Scheduler scheduler(ConfigFor(2));
    DemoWorld world(SmallOptions());
    for (u32 f = 0; f < 10; ++f)
    {
        world.Frame(&scheduler);
    }
    const u64 before = world.ScriptStats().updates;
    CHECK_FALSE(world.Reload(StringView("this is not lua (")));
    for (u32 f = 0; f < 10; ++f)
    {
        world.Frame(&scheduler);
    }
    CHECK(world.ScriptStats().updates > before);
    CHECK(world.ScriptStats().errors == 0);
}
