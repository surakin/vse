#include <catch2/catch_test_macros.hpp>

#include "alloc_counter.h"
#include "containers/vector.h"
#include "ecs/command_buffer.h"
#include "ecs/query.h"
#include "ecs/world.h"
#include "log/assert.h"
#include "test_rng.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ecs;

namespace {

struct CHealth { u32 hp; };
struct CPos { float x, y; };
struct CTag {};
struct alignas(64) CWide { float v[16]; };

// Counts constructions and destructions so a test can prove every value is destroyed exactly once.
struct CCounted {
    static inline i64 constructed = 0;
    static inline i64 destroyed = 0;
    u32 v = 0;
    CCounted() { ++constructed; }
    explicit CCounted(u32 value) : v(value) { ++constructed; }
    CCounted(CCounted&& o) noexcept : v(o.v) { ++constructed; }
    CCounted(const CCounted&) = delete;
    ~CCounted() { ++destroyed; }
    static i64 Live() { return constructed - destroyed; }
};

i32 g_command_assert_hits = 0;
AssertAction CountCommandAssert(const AssertInfo&) {
    ++g_command_assert_hits;
    return AssertAction::Continue;
}

struct Recorded {
    static inline Vector<u32>* log = nullptr;
    u32 id;
    explicit Recorded(u32 i) : id(i) {}
    Recorded(Recorded&& o) noexcept : id(o.id) { o.id = 0; }
    ~Recorded() {
        if (id != 0 && log != nullptr) log->Push(id);
    }
};

} // namespace

TEST_CASE("CommandBuffer: destroying entities from inside a query leaves exactly the marked ones gone", "[ecs][command]") {
    World world;
    for (u32 i = 0; i < 100; ++i) {
        world.Create(CHealth{i});
    }
    CommandBuffer buffer;
    auto q = world.Query<Read<CHealth>>();
    q.ForEach([&](Entity e, const CHealth& h) {
        if (h.hp % 3 == 0) {
            buffer.Destroy(e);
        }
    });
    CHECK(buffer.Size() == 34);
    CHECK(q.Count() == 100);
    const PlaybackReport report = world.Apply(buffer);
    CHECK(report.applied == 34);
    CHECK(report.ignored == 0);
    CHECK(q.Count() == 66);
    q.ForEach([](const CHealth& h) { CHECK(h.hp % 3 != 0); });
    CHECK(world.Validate().Ok());
    CHECK(buffer.IsEmpty());
}

TEST_CASE("CommandBuffer: a placeholder can be named by later commands and resolves after playback", "[ecs][command]") {
    World world;
    CommandBuffer buffer;
    const Entity placeholder = buffer.Create(CHealth{7});
    CHECK(placeholder.IsPlaceholder());
    CHECK_FALSE(buffer.Resolve(placeholder).IsValid()); // not applied yet
    buffer.Add<CPos>(placeholder, CPos{1, 2});
    buffer.Add<CTag>(placeholder, {});
    buffer.Set<CHealth>(placeholder, CHealth{9});
    const Entity empty = buffer.Create();
    buffer.Add<CHealth>(empty, CHealth{1});
    const PlaybackReport report = world.Apply(buffer);
    CHECK(report.applied == 6);
    CHECK(report.created == 2);
    const Entity real = buffer.Resolve(placeholder);
    REQUIRE(real.IsValid());
    CHECK_FALSE(real.IsPlaceholder());
    CHECK(world.Exists(real));
    CHECK_FALSE(world.Exists(placeholder));
    CHECK(world.Get<CHealth>(real)->hp == 9);
    CHECK(world.Get<CPos>(real)->y == 2.0f);
    CHECK(world.Has<CTag>(real));
    CHECK(world.Get<CHealth>(buffer.Resolve(empty))->hp == 1);
    CHECK(buffer.Resolve(real) == real); // a real entity resolves to itself
    buffer.Clear();
    CHECK_FALSE(buffer.Resolve(placeholder).IsValid());
    CHECK(world.Validate().Ok());
}

TEST_CASE("CommandBuffer: commands that cannot apply are skipped and counted", "[ecs][command]") {
    World world;
    const Entity a = world.Create(CHealth{1});
    const Entity b = world.Create(CHealth{2}, CPos{});
    const Entity dead = world.Create(CHealth{3});
    world.Destroy(dead);
    CommandBuffer buffer;
    buffer.Destroy(dead);                 // dead
    buffer.Add<CPos>(dead, CPos{});       // dead
    buffer.Remove<CPos>(a);               // missing
    buffer.Add<CHealth>(a, CHealth{5});   // already has
    buffer.Remove<CPos>(b);               // ok
    buffer.Set<CPos>(a, CPos{4, 4});      // ok (an add)
    const PlaybackReport report = world.Apply(buffer);
    CHECK(report.applied == 2);
    CHECK(report.ignored == 4);
    CHECK(world.Get<CPos>(a)->x == 4.0f);
    CHECK_FALSE(world.Has<CPos>(b));
    CHECK(world.Get<CHealth>(a)->hp == 1);
    CHECK(world.Validate().Ok());
}

TEST_CASE("CommandBuffer: a command on a placeholder whose create was skipped is ignored", "[ecs][command]") {
    World world;
    CommandBuffer buffer;
    const Entity placeholder = buffer.Create(CHealth{1});
    buffer.Destroy(placeholder);
    buffer.Add<CPos>(placeholder, CPos{});
    // A placeholder of another buffer, or a made-up one, resolves to nothing.
    buffer.Destroy(Entity{999, Entity::kPlaceholderGeneration});
    const PlaybackReport report = world.Apply(buffer);
    CHECK(report.created == 1);
    CHECK(report.applied == 2); // the create and the destroy
    CHECK(report.ignored == 2); // the add after the destroy, and the foreign placeholder
}

TEST_CASE("CommandBuffer: values are moved once and destroyed exactly once, applied or not", "[ecs][command]") {
    CCounted::constructed = 0;
    CCounted::destroyed = 0;
    {
        World world;
        const Entity e = world.Create(CHealth{1});
        {
            CommandBuffer buffer;
            for (u32 i = 0; i < 50; ++i) {
                buffer.Add<CCounted>(e, CCounted{i});
                buffer.Create(CCounted{i}, CHealth{i});
            }
            CHECK(CCounted::Live() == 100); // every value sits in the buffer, none leaked in a temporary
            buffer.Clear();                 // never applied
            CHECK(CCounted::Live() == 0);
            buffer.Add<CCounted>(e, CCounted{1});
            buffer.Create(CCounted{2}, CHealth{2});
            buffer.Create(CCounted{3});
            const PlaybackReport report = world.Apply(buffer);
            CHECK(report.applied == 3);
            CHECK(CCounted::Live() == 3); // one on e, two on the created entities; the buffer's copies are gone
            buffer.Add<CCounted>(e, CCounted{9}); // left in the buffer: destroyed with it
            CHECK(CCounted::Live() == 4);
        }
        CHECK(CCounted::Live() == 3);
        CHECK(world.Validate().Ok());
    }
    CHECK(CCounted::Live() == 0);
}

TEST_CASE("CommandBuffer: values with a large alignment survive the buffer", "[ecs][command]") {
    World world;
    CommandBuffer buffer;
    CWide wide{};
    for (u32 i = 0; i < 16; ++i) wide.v[i] = static_cast<float>(i);
    const Entity p = buffer.Create(CHealth{1}, wide, CPos{3, 4});
    world.Apply(buffer);
    const Entity real = buffer.Resolve(p);
    REQUIRE(real.IsValid());
    const CWide* got = world.Get<CWide>(real);
    REQUIRE(got != nullptr);
    CHECK(reinterpret_cast<usize>(got) % 64 == 0);
    CHECK(got->v[15] == 15.0f);
    CHECK(world.Get<CPos>(real)->y == 4.0f);
    CHECK(world.Get<CHealth>(real)->hp == 1);
}

TEST_CASE("CommandBuffer: buffers applied in the caller's order give a defined result", "[ecs][command]") {
    World world;
    const Entity e = world.Create(CHealth{0});
    CommandBuffer first;
    CommandBuffer second;
    first.Set<CHealth>(e, CHealth{1});
    second.Set<CHealth>(e, CHealth{2});
    world.Apply(first);
    world.Apply(second);
    CHECK(world.Get<CHealth>(e)->hp == 2);
    first.Set<CHealth>(e, CHealth{1});
    second.Set<CHealth>(e, CHealth{2});
    world.Apply(second);
    world.Apply(first);
    CHECK(world.Get<CHealth>(e)->hp == 1);

    CommandBufferPool pool(3);
    pool.At(0).Set<CHealth>(e, CHealth{10});
    pool.At(1).Set<CHealth>(e, CHealth{11});
    pool.At(2).Create(CHealth{12});
    const PlaybackReport report = pool.ApplyAll(world);
    CHECK(report.applied == 3);
    CHECK(report.created == 1);
    CHECK(world.Get<CHealth>(e)->hp == 11); // index order: the last writer wins
    pool.ClearAll();
}

TEST_CASE("CommandBuffer: applying during an iteration is reported", "[ecs][command]") {
    World world;
    CommandBuffer buffer;
    buffer.Create(CHealth{1});
    const AssertHandler previous = SetAssertHandler(&CountCommandAssert);
    g_command_assert_hits = 0;
    world.BeginIteration();
    world.Apply(buffer);
    world.EndIteration();
    SetAssertHandler(previous);
    CHECK(g_command_assert_hits >= 1);
}

TEST_CASE("CommandBuffer: a buffer outlives its world and works with another", "[ecs][command]") {
    CommandBuffer buffer;
    {
        World gone;
        buffer.Create(CHealth{5});
        gone.Apply(buffer);
    }
    buffer.Clear();
    World other;
    buffer.Create(CHealth{6});
    other.Apply(buffer);
    u32 seen = 0;
    other.Query<Read<CHealth>>().ForEach([&](const CHealth& h) { seen = h.hp; });
    CHECK(seen == 6);
}

TEST_CASE("CommandBuffer: 100,000 commands record without allocating after the first frame", "[ecs][command]") {
    World world;
    Vector<Entity> entities;
    for (u32 i = 0; i < 1000; ++i) {
        entities.Push(world.Create(CHealth{i}));
    }
    CommandBuffer buffer;
    auto record = [&] {
        for (u32 i = 0; i < 100'000; ++i) {
            switch (i % 4) {
            case 0: buffer.Destroy(entities[i % 1000]); break;
            case 1: buffer.Add<CPos>(entities[i % 1000], CPos{1, 2}); break;
            case 2: buffer.Remove<CPos>(entities[i % 1000]); break;
            default: buffer.Create(CHealth{i}, CPos{}); break;
            }
        }
    };
    record();
    CHECK(buffer.Size() == 100'000);
    buffer.Clear();
    const unsigned long before = EcsTestHeapAllocations();
    record();
    const unsigned long after = EcsTestHeapAllocations();
    CHECK(buffer.Size() == 100'000);
    if (EcsTestCountsAllocations()) {
        CHECK(after == before);
    }
    buffer.Clear();
}

TEST_CASE("CommandBuffer: a random command stream equals the same calls made directly", "[ecs][command]") {
    for (u64 seed = 1; seed <= 200; ++seed) {
        ecs_test::Rng rng(seed);
        World direct;
        World buffered;
        Vector<Entity> direct_ids;     // entities of the direct world, by creation order
        Vector<Entity> buffered_ids;   // the same entities as the buffer sees them (real, or placeholders)
        for (u32 i = 0; i < 20; ++i) {
            direct_ids.Push(direct.Create(CHealth{i}));
            buffered_ids.Push(buffered.Create(CHealth{i}));
        }
        CommandBuffer buffer;
        u32 direct_ok = 0;
        const u32 ops = 20 + rng.Below(80);
        for (u32 i = 0; i < ops; ++i) {
            const u32 pick = rng.Below(6);
            const usize at = rng.Below(static_cast<u32>(direct_ids.Size()));
            if (pick == 0) {
                if (direct.Destroy(direct_ids[at]) == Status::Ok) ++direct_ok;
                buffer.Destroy(buffered_ids[at]);
            } else if (pick == 1) {
                if (direct.Add<CPos>(direct_ids[at], CPos{1, 1}) == Status::Ok) ++direct_ok;
                buffer.Add<CPos>(buffered_ids[at], CPos{1, 1});
            } else if (pick == 2) {
                if (direct.Remove<CPos>(direct_ids[at]) == Status::Ok) ++direct_ok;
                buffer.Remove<CPos>(buffered_ids[at]);
            } else if (pick == 3) {
                if (direct.Set<CTag>(direct_ids[at], {}) == Status::Ok) ++direct_ok;
                buffer.Set<CTag>(buffered_ids[at], {});
            } else {
                direct_ids.Push(direct.Create(CHealth{99}, CPos{}));
                ++direct_ok;
                buffered_ids.Push(buffer.Create(CHealth{99}, CPos{}));
            }
        }
        const PlaybackReport report = buffered.Apply(buffer);
        CHECK(report.applied == direct_ok);
        CHECK(report.applied + report.ignored == ops);
        auto count = [](World& w) {
            u32 n = 0;
            w.Query<Read<CHealth>>().ForEach([&](const CHealth&) { ++n; });
            return n;
        };
        auto count_pos = [](World& w) {
            u32 n = 0;
            w.Query<Read<CPos>>().ForEach([&](const CPos&) { ++n; });
            return n;
        };
        CHECK(count(buffered) == count(direct));
        CHECK(count_pos(buffered) == count_pos(direct));
        CHECK(buffered.Stats().entities == direct.Stats().entities);
        REQUIRE(buffered.Validate().Ok());
    }
}

TEST_CASE("Resources: set, get, replace in place, remove", "[ecs][resource]") {
    struct Gravity { float g; };
    World world;
    CHECK_FALSE(world.HasResource<Gravity>());
    CHECK(world.TryResource<Gravity>() == nullptr);
    Gravity& first = world.SetResource(Gravity{9.8f});
    CHECK(world.HasResource<Gravity>());
    CHECK(world.Resource<Gravity>().g == 9.8f);
    first.g = 1.0f;
    Gravity& replaced = world.SetResource(Gravity{3.0f});
    CHECK(&replaced == &first); // replaced in place: the address does not change
    CHECK(world.Resource<Gravity>().g == 3.0f);
    const World& view = world;
    CHECK(view.Resource<Gravity>().g == 3.0f);
    CHECK(view.TryResource<Gravity>() != nullptr);
    CHECK(world.RemoveResource<Gravity>());
    CHECK_FALSE(world.RemoveResource<Gravity>());
    CHECK_FALSE(world.HasResource<Gravity>());
    world.SetResource(Gravity{2.0f});
    CHECK(world.Resource<Gravity>().g == 2.0f);
}

TEST_CASE("Resources: a missing resource is reported", "[ecs][resource]") {
    struct Missing { int v; };
    World world;
    const AssertHandler previous = SetAssertHandler(&CountCommandAssert);
    g_command_assert_hits = 0;
    // The death-test harness lives in the debug test suite, which these tests do not link: the counting handler proves the assert fires.
    // The result is not used (a continuing handler would hand back a null reference).
    Missing* none = world.TryResource<Missing>();
    CHECK(none == nullptr);
    if (none == nullptr) {
        ENGINE_ASSERT(none != nullptr, "World::Resource: the world has no resource of this type");
    }
    SetAssertHandler(previous);
    CHECK(g_command_assert_hits == 1);
}

TEST_CASE("Resources: destroyed newest first, replaced and removed ones at once", "[ecs][resource]") {
    struct First : Recorded { First() : Recorded(1) {} First(First&&) = default; };
    struct Second : Recorded { Second() : Recorded(2) {} Second(Second&&) = default; };
    struct Third : Recorded { Third() : Recorded(3) {} Third(Third&&) = default; };
    Vector<u32> log;
    Recorded::log = &log;
    {
        World world;
        world.SetResource(First{});
        world.SetResource(Second{});
        world.SetResource(Third{});
        CHECK(log.Size() == 0); // the moved-from temporaries destroy silently
        world.RemoveResource<Second>();
        REQUIRE(log.Size() == 1);
        CHECK(log[0] == 2);
        world.SetResource(Second{}); // made again, now the newest
        world.SetResource(First{});  // a replacement destroys the old value now, and keeps its place in the order
        REQUIRE(log.Size() == 2);
        CHECK(log[1] == 1);
    }
    // Destruction order of the rest: Second (newest), Third, First.
    REQUIRE(log.Size() == 5);
    CHECK(log[2] == 2);
    CHECK(log[3] == 3);
    CHECK(log[4] == 1);
    Recorded::log = nullptr;
}

TEST_CASE("Resources: a resource with a large alignment is aligned and a world with resources and entities destroys cleanly", "[ecs][resource]") {
    World world;
    CWide& wide = world.SetResource(CWide{});
    CHECK(reinterpret_cast<usize>(&wide) % 64 == 0);
    world.Create(CHealth{1});
    world.SetResource(CPos{1, 2});
    CHECK(world.Resource<CPos>().y == 2.0f);
    CHECK(world.Validate().Ok());
}
