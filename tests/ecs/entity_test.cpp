#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "ecs/entity.h"
#include "ecs/entity_table.h"
#include "lang/utility.h"
#include "sync/atomic.h"
#include "sync/thread.h"
#include "test_rng.h"

using namespace vse;
using namespace vse::ecs;

namespace
{
EntityLocation Where(u32 row = 0)
{
    return EntityLocation{0, 0, row};
}
}  // namespace

TEST_CASE("Entity: the default is invalid and packing round-trips", "[ecs][entity]")
{
    Entity none;
    CHECK_FALSE(none.IsValid());
    CHECK(none.ToU64() == 0);
    const Entity e{7, 3};
    CHECK(e.IsValid());
    CHECK(e.ToU64() == ((u64{3} << 32) | 7));
    CHECK(Entity::FromU64(e.ToU64()) == e);
    CHECK(Entity{7, 3} != Entity{7, 4});
    CHECK(Entity{7, 3} != Entity{8, 3});
    CHECK(Entity{9, 1} < Entity{1, 2});
}

TEST_CASE("EntityTable: fresh ids are valid, alive and distinct", "[ecs][entity]")
{
    EntityTable table;
    Vector<Entity> ids;
    for (u32 i = 0; i < 1000; ++i)
    {
        const Entity e = table.Allocate(Where(i));
        REQUIRE(e.IsValid());
        REQUIRE(table.IsAlive(e));
        ids.Push(e);
    }
    for (usize i = 0; i < ids.Size(); ++i)
    {
        for (usize j = i + 1; j < ids.Size(); ++j)
        {
            REQUIRE(ids[i] != ids[j]);
        }
    }
    CHECK(table.Stats().alive == 1000);
    CHECK(table.Location(ids[10])->row == 10);
}

TEST_CASE("EntityTable: a freed id is dead and the reused slot gives a different id", "[ecs][entity]")
{
    EntityTable table;
    const Entity a = table.Allocate(Where(1));
    REQUIRE(table.Free(a));
    CHECK_FALSE(table.IsAlive(a));
    CHECK(table.Location(a) == nullptr);
    CHECK_FALSE(table.Free(a));
    const Entity b = table.Allocate(Where(2));
    CHECK(b.index == a.index);
    CHECK(b.generation == a.generation + 1);
    CHECK(b != a);
    CHECK(table.IsAlive(b));
    CHECK_FALSE(table.IsAlive(a));
    CHECK(table.Stats().slots == 1);
}

TEST_CASE("EntityTable: slots are reused last in, first out", "[ecs][entity]")
{
    EntityTable table;
    const Entity a = table.Allocate(Where());
    const Entity b = table.Allocate(Where());
    const Entity c = table.Allocate(Where());
    table.Free(a);
    table.Free(c);
    CHECK(table.Allocate(Where()).index == c.index);
    CHECK(table.Allocate(Where()).index == a.index);
    CHECK(table.Allocate(Where()).index != b.index);
}

TEST_CASE("EntityTable: invalid and out-of-range ids are not alive", "[ecs][entity]")
{
    EntityTable table;
    const Entity a = table.Allocate(Where());
    CHECK_FALSE(table.IsAlive(Entity{}));
    CHECK_FALSE(table.IsAlive(Entity{a.index, 0}));
    CHECK_FALSE(table.IsAlive(Entity{a.index + 1, 1}));
    CHECK_FALSE(table.IsAlive(Entity{0xFFFFFFFFu, 1}));
    CHECK_FALSE(table.IsAlive(Entity{a.index, a.generation + 1}));
    CHECK_FALSE(table.Free(Entity{}));
    CHECK_FALSE(table.Free(Entity{99, 1}));
    CHECK(table.Location(Entity{99, 1}) == nullptr);
    // A forged id that matches a free slot's next generation is still dead.
    table.Free(a);
    CHECK_FALSE(table.IsAlive(Entity{a.index, a.generation + 1}));
}

TEST_CASE("EntityTable: locations are stored and can be changed", "[ecs][entity]")
{
    EntityTable table;
    const Entity e = table.Allocate(EntityLocation{2, 3, 4});
    const EntityTable& view = table;
    REQUIRE(view.Location(e) != nullptr);
    CHECK(view.Location(e)->archetype == 2);
    CHECK(view.Location(e)->chunk == 3);
    CHECK(view.Location(e)->row == 4);
    table.Location(e)->row = 9;
    CHECK(view.Location(e)->row == 9);
}

TEST_CASE("EntityTable: a million create and destroy cycles stay exact against a model", "[ecs][entity]")
{
    EntityTable table;
    ecs_test::Rng rng(12345);
    Vector<Entity> live;
    Vector<Entity> stale;  // a ring of recently destroyed ids, which must stay dead
    stale.Resize(64);
    usize stale_next = 0;
    for (u32 step = 0; step < 1000000; ++step)
    {
        const bool create = live.Size() == 0 || rng.Below(100) < 52;
        if (create)
        {
            const Entity e = table.Allocate(Where(step));
            REQUIRE(e.IsValid());
            live.Push(e);
        }
        else
        {
            const u32 pick = rng.Below(static_cast<u32>(live.Size()));
            const Entity e = live[pick];
            REQUIRE(table.Free(e));
            live[pick] = live[live.Size() - 1];
            live.Pop();
            stale[stale_next] = e;
            stale_next = (stale_next + 1) % stale.Size();
        }
        if (step % 4096 == 0)
        {
            for (usize i = 0; i < stale.Size(); ++i)
            {
                if (stale[i].IsValid())
                {
                    REQUIRE_FALSE(table.IsAlive(stale[i]));
                }
            }
        }
    }
    CHECK(table.Stats().alive == live.Size());
    CHECK(table.Stats().alive + table.Stats().free == table.Stats().slots);
    for (usize i = 0; i < live.Size(); ++i)
    {
        REQUIRE(table.IsAlive(live[i]));
    }
}

TEST_CASE("EntityTable: the generation wraps past zero and the wrap is counted", "[ecs][entity]")
{
    EntityTable table;
    const Entity a = table.Allocate(Where());
    table.SetGenerationForTesting(
        a.index, 0xFFFFFFFEu);  // the last generation that is issued; 0xFFFFFFFF marks command-buffer placeholders
    const Entity old{a.index, 0xFFFFFFFEu};
    REQUIRE(table.IsAlive(old));
    REQUIRE(table.Free(old));
    CHECK(table.Stats().generation_wraps == 1);
    const Entity next = table.Allocate(Where());
    CHECK(next.index == a.index);
    CHECK(next.generation == 1);
    CHECK(next.IsValid());
    CHECK_FALSE(next.IsPlaceholder());
    CHECK_FALSE(table.IsAlive(old));
    CHECK_FALSE(table.IsAlive(Entity{a.index, Entity::kPlaceholderGeneration}));
}

TEST_CASE("EntityTable: a moved-from table is empty and consistent", "[ecs][entity]")
{
    EntityTable a;
    const Entity e = a.Allocate(Where(5));
    const Entity f = a.Allocate(Where(6));
    a.Free(f);
    EntityTable b(Move(a));
    CHECK(b.IsAlive(e));
    CHECK(b.Stats().alive == 1);
    CHECK(a.Stats().alive == 0);
    CHECK(a.Stats().slots == 0);
    CHECK(a.Stats().free == 0);
    CHECK_FALSE(a.IsAlive(e));
    EntityTable c;
    c = Move(b);
    CHECK(c.IsAlive(e));
    CHECK(b.Stats().alive == 0);
    CHECK(c.Stats().alive == 1);
}

TEST_CASE("EntityTable: a full table refuses and recovers after a free", "[ecs][entity]")
{
    EntityTable table;
    table.SetMaxSlotsForTesting(4);
    Vector<Entity> ids;
    for (int i = 0; i < 4; ++i)
    {
        ids.Push(table.Allocate(Where()));
        REQUIRE(ids[i].IsValid());
    }
    const Entity refused = table.Allocate(Where());
    CHECK_FALSE(refused.IsValid());
    CHECK(table.Stats().refused == 1);
    CHECK(table.Stats().alive == 4);
    REQUIRE(table.Free(ids[2]));
    const Entity again = table.Allocate(Where());
    CHECK(again.IsValid());
    CHECK(again.index == ids[2].index);
}

TEST_CASE("Hash<Entity>: sequential ids spread over the buckets", "[ecs][entity]")
{
    constexpr u32 kBuckets = 1u << 14;
    constexpr u32 kIds = 100000;
    Vector<u32> counts;
    counts.Resize(kBuckets);
    for (u32 i = 0; i < kBuckets; ++i)
    {
        counts[i] = 0;
    }
    for (u32 i = 0; i < kIds; ++i)
    {
        ++counts[Hash<Entity>{}(Entity{i, 1}) % kBuckets];
    }
    const u32 mean2 = 2 * kIds / kBuckets;
    u32 overloaded = 0;
    for (u32 i = 0; i < kBuckets; ++i)
    {
        if (counts[i] > mean2)
        {
            ++overloaded;
        }
    }
    CHECK(overloaded * 20 <= kBuckets);  // at most 5 percent of the buckets hold more than twice the mean
    CHECK(Hash<Entity>{}(Entity{5, 1}) != Hash<Entity>{}(Entity{5, 2}));
}

TEST_CASE("EntityTable: concurrent readers see a stable table", "[ecs][entity][tsan]")
{
    EntityTable table;
    Vector<Entity> live;
    Vector<Entity> dead;
    for (u32 i = 0; i < 2000; ++i)
    {
        live.Push(table.Allocate(Where(i)));
    }
    for (u32 i = 0; i < 500; ++i)
    {
        const Entity e = live[i];
        table.Free(e);
        dead.Push(e);
    }
    const EntityTable& view = table;
    Atomic<u32> wrong{0};
    Vector<Thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.Push(Thread(
            [&]
            {
                for (int rep = 0; rep < 200; ++rep)
                {
                    for (usize i = 500; i < live.Size(); ++i)
                    {
                        if (!view.IsAlive(live[i]) || view.Location(live[i])->row != static_cast<u32>(i))
                        {
                            wrong.FetchAdd(1u);
                        }
                    }
                    for (usize i = 0; i < dead.Size(); ++i)
                    {
                        if (view.IsAlive(dead[i]))
                        {
                            wrong.FetchAdd(1u);
                        }
                    }
                }
            }));
    }
    for (usize i = 0; i < threads.Size(); ++i)
    {
        threads[i].Join();
    }
    CHECK(wrong.Load() == 0);
}
