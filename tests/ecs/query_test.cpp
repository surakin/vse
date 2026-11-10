#include <catch2/catch_test_macros.hpp>

#include "alloc_counter.h"
#include "algorithm.h"
#include "containers/vector.h"
#include "ecs/query.h"
#include "ecs/world.h"
#include "log/assert.h"
#include "test_rng.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ecs;

namespace {

struct QPos { float x, y, z; };
struct QVel { float x, y, z; };
struct QHealth { u32 hp; };
struct QMass { float kg; };
struct QFrozen {};
struct QTeam {};

i32 g_query_assert_hits = 0;
AssertAction CountQueryAssert(const AssertInfo&) {
    ++g_query_assert_hits;
    return AssertAction::Continue;
}

u64 Key(Entity e) { return e.ToU64(); }

} // namespace

TEST_CASE("Query: matches exactly the archetypes with every required term and none of the excluded", "[ecs][query]") {
    World world;
    const Entity a = world.Create(QPos{1, 0, 0}, QVel{});
    const Entity b = world.Create(QPos{2, 0, 0});
    const Entity c = world.Create(QPos{3, 0, 0}, QVel{}, QFrozen{});
    const Entity d = world.Create(QVel{});
    const Entity e = world.Create(QPos{5, 0, 0}, QVel{}, QHealth{9});

    auto moving = world.Query<Write<QPos>, Read<QVel>, Without<QFrozen>>();
    Vector<u64> seen;
    moving.ForEach([&](Entity entity, QPos&, const QVel&) { seen.Push(Key(entity)); });
    Sort(Span<u64>(seen.Data(), seen.Size()), [](u64 x, u64 y) { return x < y; });
    Vector<u64> expected;
    expected.Push(Key(a));
    expected.Push(Key(e));
    Sort(Span<u64>(expected.Data(), expected.Size()), [](u64 x, u64 y) { return x < y; });
    REQUIRE(seen.Size() == 2);
    CHECK(seen[0] == expected[0]);
    CHECK(seen[1] == expected[1]);
    CHECK(moving.Count() == 2);
    CHECK(moving.MatchedArchetypes() == 2);

    auto frozen_only = world.Query<With<QFrozen>>();
    CHECK(frozen_only.Count() == 1);
    auto no_position = world.Query<Read<QVel>, Without<QPos>>();
    u32 hits = 0;
    no_position.ForEach([&](Entity entity, const QVel&) {
        CHECK(entity == d);
        ++hits;
    });
    CHECK(hits == 1);
    (void)b;
    (void)c;
}

TEST_CASE("Query: optional terms give null or a pointer, and OptionalWrite writes", "[ecs][query]") {
    World world;
    world.Create(QPos{1, 0, 0});
    world.Create(QPos{2, 0, 0}, QMass{10});
    auto q = world.Query<Read<QPos>, Optional<QMass>>();
    u32 with_mass = 0;
    u32 without_mass = 0;
    q.ForEach([&](const QPos& p, const QMass* m) {
        if (m != nullptr) {
            ++with_mass;
            CHECK(p.x == 2.0f);
            CHECK(m->kg == 10.0f);
        } else {
            ++without_mass;
            CHECK(p.x == 1.0f);
        }
    });
    CHECK(with_mass == 1);
    CHECK(without_mass == 1);
    CHECK(q.MatchedArchetypes() == 2); // an optional term does not narrow the match

    auto w = world.Query<Read<QPos>, OptionalWrite<QMass>>();
    w.ForEach([](const QPos&, QMass* m) {
        if (m != nullptr) {
            m->kg += 5.0f;
        }
    });
    float total = 0;
    world.Query<Read<QMass>>().ForEach([&](const QMass& m) { total += m.kg; });
    CHECK(total == 15.0f);
}

TEST_CASE("Query: entities in archetypes created after the query are seen", "[ecs][query]") {
    World world;
    world.Create(QPos{1, 0, 0});
    auto q = world.Query<Read<QPos>>();
    CHECK(q.Count() == 1);
    CHECK(q.MatchedArchetypes() == 1);
    world.Create(QPos{2, 0, 0}, QHealth{1}); // a new archetype
    world.Create(QHealth{3});                // a new archetype that does not match
    CHECK(q.Count() == 2);
    CHECK(q.MatchedArchetypes() == 2);
    const u32 archetypes = world.ArchetypeCount();
    CHECK(q.Count() == 2); // nothing new: the same answer without re-matching
    CHECK(world.ArchetypeCount() == archetypes);
}

TEST_CASE("Query: an archetype that became empty stays listed and is skipped", "[ecs][query]") {
    World world;
    const Entity e = world.Create(QPos{1, 0, 0}, QHealth{1});
    world.Create(QPos{2, 0, 0});
    auto q = world.Query<Read<QPos>>();
    CHECK(q.MatchedArchetypes() == 2);
    world.Destroy(e);
    CHECK(q.MatchedArchetypes() == 2);
    CHECK(q.Count() == 1);
    u32 visited = 0;
    q.ForEach([&](const QPos&) { ++visited; });
    q.ForEachChunk([&](ChunkView& chunk) { visited += chunk.Count() * 10; });
    CHECK(visited == 11);
    CHECK_FALSE(q.IsEmpty());
}

TEST_CASE("Query: the visiting order is chunk order and is the same in two identical worlds", "[ecs][query]") {
    auto run = [] {
        World world;
        for (u32 i = 0; i < 3000; ++i) {
            world.Create(QPos{static_cast<float>(i), 0, 0}, QVel{});
            if (i % 7 == 0) {
                world.Create(QPos{static_cast<float>(10000 + i), 0, 0});
            }
        }
        Vector<float> order;
        world.Query<Read<QPos>>().ForEach([&](const QPos& p) { order.Push(p.x); });
        return order;
    };
    const Vector<float> first = run();
    const Vector<float> second = run();
    REQUIRE(first.Size() == second.Size());
    for (usize i = 0; i < first.Size(); ++i) {
        CHECK(first[i] == second[i]);
    }
    // Within the {QPos, QVel} archetype the rows come out in creation order.
    float last = -1;
    for (usize i = 0; i < first.Size(); ++i) {
        if (first[i] < 10000) {
            CHECK(first[i] > last);
            last = first[i];
        }
    }
}

TEST_CASE("Query: ForEachChunk spans have the chunk's count and alias the memory ForEach mutates", "[ecs][query]") {
    World world;
    constexpr u32 kCount = 5000; // more than one chunk
    world.CreateMany<QPos, QVel>(kCount, [](u32 i, QPos& p, QVel& v) {
        p = QPos{static_cast<float>(i), 0, 0};
        v = QVel{1, 0, 0};
    });
    auto q = world.Query<Write<QPos>, Read<QVel>>();

    u32 chunks = 0;
    u32 rows = 0;
    q.ForEachChunk([&](ChunkView& chunk) {
        ++chunks;
        Span<QPos> positions = chunk.Write<QPos>();
        Span<const QVel> velocities = chunk.Read<QVel>();
        Span<Entity> entities = chunk.Entities();
        CHECK(positions.Size() == chunk.Count());
        CHECK(velocities.Size() == chunk.Count());
        CHECK(entities.Size() == chunk.Count());
        for (u32 i = 0; i < chunk.Count(); ++i) {
            positions[i].y = 100.0f; // written through the span...
        }
        rows += chunk.Count();
    });
    CHECK(chunks >= 2);
    CHECK(rows == kCount);

    u32 seen = 0;
    q.ForEach([&](QPos& p, const QVel& v) { // ...and visible through ForEach, which also mutates the same memory
        CHECK(p.y == 100.0f);
        p.x += v.x;
        ++seen;
    });
    CHECK(seen == kCount);
    float sum = 0;
    q.ForEachChunk([&](ChunkView& chunk) {
        for (const QPos& p : chunk.Read<QPos>()) {
            sum += p.x;
        }
    });
    CHECK(sum == static_cast<float>(kCount) * (kCount - 1) / 2.0f + static_cast<float>(kCount));
}

TEST_CASE("Query: a chunk view gives an empty span for an absent optional column", "[ecs][query]") {
    World world;
    world.Create(QPos{1, 0, 0});
    world.Create(QPos{2, 0, 0}, QMass{4});
    auto q = world.Query<Read<QPos>, OptionalWrite<QMass>>();
    u32 empty = 0;
    u32 full = 0;
    q.ForEachChunk([&](ChunkView& chunk) {
        if (chunk.Read<QMass>().Size() == 0) {
            ++empty;
            CHECK(chunk.Write<QMass>().Size() == 0);
        } else {
            ++full;
            chunk.Write<QMass>()[0].kg = 8;
        }
    });
    CHECK(empty == 1);
    CHECK(full == 1);
}

TEST_CASE("Query: returning false stops, and the iteration guard is released every way out", "[ecs][query]") {
    World world;
    for (u32 i = 0; i < 100; ++i) {
        world.Create(QPos{static_cast<float>(i), 0, 0});
    }
    auto q = world.Query<Read<QPos>>();

    u32 visited = 0;
    q.ForEach([&](const QPos&) {
        CHECK(world.IsIterating());
        return ++visited < 10;
    });
    CHECK(visited == 10);
    CHECK_FALSE(world.IsIterating());

    u32 chunks = 0;
    q.ForEachChunk([&](ChunkView&) {
        ++chunks;
        return false;
    });
    CHECK(chunks == 1);
    CHECK_FALSE(world.IsIterating());

    visited = 0;
    q.ForEach([&](const QPos&) { ++visited; }); // a void function visits everything
    CHECK(visited == 100);
    world.Create(QPos{}); // structural changes work again
    CHECK(q.Count() == 101);
}

TEST_CASE("Query: structural calls and Get of a written component during iteration are reported", "[ecs][query]") {
    World world;
    const Entity e = world.Create(QPos{1, 0, 0}, QVel{});
    auto q = world.Query<Write<QPos>, Read<QVel>>();
    const AssertHandler previous = SetAssertHandler(&CountQueryAssert);
    g_query_assert_hits = 0;
    q.ForEach([&](QPos&, const QVel&) {
        world.Create();                        // structural: reported (the test handler lets it go on)
        CHECK(world.Get<QVel>(e) != nullptr);  // reading a component the query only reads is allowed
        CHECK(g_query_assert_hits == 1);
        world.Get<QPos>(e);                    // the query writes QPos: reported
        CHECK(g_query_assert_hits == 2);
    });
    SetAssertHandler(previous);
    CHECK(g_query_assert_hits == 2);
    CHECK_FALSE(world.IsIterating());
    CHECK(world.Get<QPos>(e) != nullptr); // allowed again once the query is done
}

TEST_CASE("Query: nested queries over different data work and restore the write set", "[ecs][query]") {
    World world;
    const Entity e = world.Create(QPos{1, 0, 0}, QVel{2, 0, 0});
    auto outer = world.Query<Write<QPos>>();
    auto inner = world.Query<Write<QVel>>();
    outer.ForEach([&](QPos& p) {
        inner.ForEach([&](QVel& v) { v.x += 1; });
        p.x += 1;
        const AssertHandler previous = SetAssertHandler(&CountQueryAssert);
        g_query_assert_hits = 0;
        CHECK(world.Get<QVel>(e) != nullptr); // the inner query is done: QVel is free again
        CHECK(g_query_assert_hits == 0);
        world.Get<QPos>(e);                   // the outer query still writes QPos
        CHECK(g_query_assert_hits == 1);
        SetAssertHandler(previous);
    });
    CHECK(world.Get<QPos>(e)->x == 2.0f);
    CHECK(world.Get<QVel>(e)->x == 3.0f);
}

TEST_CASE("Query: random worlds agree with a brute-force filter", "[ecs][query]") {
    for (u64 seed = 1; seed <= 1000; ++seed) {
        ecs_test::Rng rng(seed);
        World world;
        Vector<Entity> alive;
        const u32 operations = 10 + rng.Below(40);
        for (u32 i = 0; i < operations; ++i) {
            const u32 pick = rng.Below(10);
            if (pick < 4 || alive.Size() == 0) {
                Entity e = world.Create(QHealth{static_cast<u32>(alive.Size())});
                if (rng.Below(2) != 0) world.Add<QPos>(e, QPos{static_cast<float>(e.index), 0, 0});
                if (rng.Below(2) != 0) world.Add<QVel>(e, QVel{});
                if (rng.Below(3) == 0) world.Add<QFrozen>(e, {});
                alive.Push(e);
            } else if (pick < 6) {
                const usize at = rng.Below(static_cast<u32>(alive.Size()));
                world.Destroy(alive[at]);
                alive.Remove(at);
            } else if (pick < 8) {
                world.Set<QVel>(alive[rng.Below(static_cast<u32>(alive.Size()))], QVel{});
            } else {
                world.Remove<QPos>(alive[rng.Below(static_cast<u32>(alive.Size()))]);
            }
        }

        auto q1 = world.Query<Read<QHealth>, Read<QPos>, Without<QFrozen>>();
        u32 expected1 = 0;
        for (usize i = 0; i < alive.Size(); ++i) {
            if (world.Has<QPos>(alive[i]) && !world.Has<QFrozen>(alive[i])) ++expected1;
        }
        u32 got1 = 0;
        q1.ForEach([&](Entity e, const QHealth&, const QPos& p) {
            ++got1;
            CHECK(world.Exists(e));
            CHECK(p.x == static_cast<float>(e.index));
        });
        CHECK(got1 == expected1);
        CHECK(q1.Count() == expected1);

        auto q2 = world.Query<Read<QHealth>, Optional<QVel>, With<QFrozen>>();
        u32 expected2 = 0;
        u32 expected_with_vel = 0;
        for (usize i = 0; i < alive.Size(); ++i) {
            if (world.Has<QFrozen>(alive[i])) {
                ++expected2;
                if (world.Has<QVel>(alive[i])) ++expected_with_vel;
            }
        }
        u32 got2 = 0;
        u32 got_with_vel = 0;
        q2.ForEach([&](const QHealth&, const QVel* v) {
            ++got2;
            if (v != nullptr) ++got_with_vel;
        });
        CHECK(got2 == expected2);
        CHECK(got_with_vel == expected_with_vel);
        REQUIRE(world.Validate().Ok());
    }
}

TEST_CASE("Query: iterating a million entities allocates nothing", "[ecs][query]") {
    World world;
    constexpr u32 kCount = 1'000'000;
    world.CreateMany<QPos, QVel>(kCount, [](u32 i, QPos& p, QVel& v) {
        p = QPos{static_cast<float>(i % 16), 0, 0};
        v = QVel{1, 0, 0};
    });
    auto q = world.Query<Write<QPos>, Read<QVel>>();
    CHECK(q.Count() == kCount); // the first use matches the archetypes (and may allocate)

    const unsigned long before = EcsTestHeapAllocations();
    u64 visited = 0;
    q.ForEach([&](QPos& p, const QVel& v) {
        p.x += v.x;
        ++visited;
    });
    u64 rows = 0;
    q.ForEachChunk([&](ChunkView& chunk) { rows += chunk.Count(); });
    const unsigned long after = EcsTestHeapAllocations();
    CHECK(visited == kCount);
    CHECK(rows == kCount);
    if (EcsTestCountsAllocations()) {
        CHECK(after == before);
    }
}
