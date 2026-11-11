#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "ecs/world.h"
#include "log/assert.h"
#include "test_rng.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ecs;

namespace
{

struct WPos
{
    float x, y;
};
struct WFrozen
{
};
struct WCounted
{
    static inline i64 live = 0;
    u32 v = 0;
    WCounted() { ++live; }
    explicit WCounted(u32 value) : v(value) { ++live; }
    WCounted(WCounted&& o) noexcept : v(o.v) { ++live; }
    WCounted(const WCounted&) = delete;
    ~WCounted() { --live; }
};

template <int N> struct M
{
    u32 v = 0;
};
template <> struct M<3>
{
    u32 v = 0;
    M() { ++WCounted::live; }
    explicit M(u32 value) : v(value) { ++WCounted::live; }
    M(M&& o) noexcept : v(o.v) { ++WCounted::live; }
    M(const M&) = delete;
    ~M() { --WCounted::live; }
};
template <> struct alignas(16) M<4>
{
    u32 v = 0;
};

i32 g_assert_hits = 0;
AssertAction CountAndContinue(const AssertInfo&)
{
    ++g_assert_hits;
    return AssertAction::Continue;
}

}  // namespace

TEST_CASE("World: create, exists, destroy and stale ids", "[ecs][world]")
{
    World world;
    const Entity a = world.Create();
    const Entity b = world.Create(WPos{1, 2});
    REQUIRE(a.IsValid());
    REQUIRE(b.IsValid());
    CHECK(world.Exists(a));
    CHECK(world.Destroy(a) == Status::Ok);
    CHECK_FALSE(world.Exists(a));
    CHECK(world.Destroy(a) == Status::DeadEntity);
    CHECK(world.Destroy(Entity{}) == Status::DeadEntity);
    CHECK(world.Get<WPos>(b)->y == 2);
    const Entity c = world.Create();
    CHECK(c.index == a.index);
    CHECK_FALSE(world.Exists(a));
    CHECK(world.Validate().Ok());
}

TEST_CASE("World: add, remove, set and get with trivial, non-trivial and tag components", "[ecs][world]")
{
    WCounted::live = 0;
    {
        World world;
        const Entity e = world.Create(WPos{1, 2});
        CHECK(world.Add<WCounted>(e, WCounted{7}) == Status::Ok);
        CHECK(world.Add<WCounted>(e, WCounted{8}) == Status::AlreadyHas);
        CHECK(world.Add<WFrozen>(e, WFrozen{}) == Status::Ok);
        CHECK(world.Has<WFrozen>(e));
        CHECK(world.Get<WFrozen>(e) == nullptr);
        CHECK(world.Get<WPos>(e)->x == 1);
        CHECK(world.Get<WCounted>(e)->v == 7);
        CHECK(WCounted::live == 1);
        CHECK(world.Set<WCounted>(e, WCounted{9}) == Status::Ok);
        CHECK(world.Get<WCounted>(e)->v == 9);
        CHECK(WCounted::live == 1);
        CHECK(world.Remove<WCounted>(e) == Status::Ok);
        CHECK(WCounted::live == 0);
        CHECK(world.Remove<WCounted>(e) == Status::MissingComponent);
        CHECK_FALSE(world.Has<WCounted>(e));
        CHECK(world.Get<WCounted>(e) == nullptr);
        CHECK(world.Set<WPos>(e, WPos{5, 6}) == Status::Ok);
        CHECK(world.Get<WPos>(e)->x == 5);
        CHECK(world.Set<WCounted>(e, WCounted{1}) == Status::Ok);  // adds
        CHECK(world.Has<WCounted>(e));
        CHECK(world.Validate().Ok());
        world.Destroy(e);
        CHECK(world.Add<WPos>(e, WPos{}) == Status::DeadEntity);
        CHECK(world.Remove<WPos>(e) == Status::DeadEntity);
        CHECK(world.Set<WPos>(e, WPos{}) == Status::DeadEntity);
        CHECK_FALSE(world.Has<WPos>(e));
        CHECK(world.Get<WPos>(e) == nullptr);
        const Entity f = world.Create(WCounted{3});
        (void)f;
    }
    CHECK(WCounted::live == 0);
}

TEST_CASE("World: a migration keeps the entity's and the neighbor's values", "[ecs][world]")
{
    World world;
    Entity ids[5];
    for (u32 i = 0; i < 5; ++i)
    {
        ids[i] = world.Create(M<0>{i}, M<1>{i * 10});
    }
    // ids[1] leaves; ids[4] is swapped into its row in the old archetype
    REQUIRE(world.Add<M<2>>(ids[1], M<2>{99}) == Status::Ok);
    CHECK(world.Get<M<0>>(ids[1])->v == 1);
    CHECK(world.Get<M<1>>(ids[1])->v == 10);
    CHECK(world.Get<M<2>>(ids[1])->v == 99);
    for (u32 i : {0u, 2u, 3u, 4u})
    {
        CHECK(world.Get<M<0>>(ids[i])->v == i);
        CHECK(world.Get<M<1>>(ids[i])->v == i * 10);
    }
    REQUIRE(world.Remove<M<1>>(ids[0]) == Status::Ok);
    CHECK(world.Get<M<0>>(ids[0])->v == 0);
    CHECK(world.Get<M<1>>(ids[0]) == nullptr);
    CHECK(world.Get<M<1>>(ids[4])->v == 40);
    CHECK(world.Validate().Ok());
}

TEST_CASE("World: the archetype graph is reused and the archetype count is bounded", "[ecs][world]")
{
    World world;
    ecs_test::Rng rng(77);
    Vector<Entity> ids;
    for (u32 i = 0; i < 200; ++i)
    {
        ids.Push(world.Create());
    }
    auto toggle = [&](Entity e, u32 k)
    {
        switch (k)
        {
        case 0:
            if (!world.Has<M<0>>(e))
            {
                world.Add<M<0>>(e, {});
            }
            else
            {
                world.Remove<M<0>>(e);
            }
            break;
        case 1:
            if (!world.Has<M<1>>(e))
            {
                world.Add<M<1>>(e, {});
            }
            else
            {
                world.Remove<M<1>>(e);
            }
            break;
        case 2:
            if (!world.Has<M<2>>(e))
            {
                world.Add<M<2>>(e, {});
            }
            else
            {
                world.Remove<M<2>>(e);
            }
            break;
        case 3:
            if (!world.Has<M<3>>(e))
            {
                world.Add<M<3>>(e, {});
            }
            else
            {
                world.Remove<M<3>>(e);
            }
            break;
        case 4:
            if (!world.Has<M<4>>(e))
            {
                world.Add<M<4>>(e, {});
            }
            else
            {
                world.Remove<M<4>>(e);
            }
            break;
        case 5:
            if (!world.Has<M<5>>(e))
            {
                world.Add<M<5>>(e, {});
            }
            else
            {
                world.Remove<M<5>>(e);
            }
            break;
        case 6:
            if (!world.Has<M<6>>(e))
            {
                world.Add<M<6>>(e, {});
            }
            else
            {
                world.Remove<M<6>>(e);
            }
            break;
        default:
            if (!world.Has<M<7>>(e))
            {
                world.Add<M<7>>(e, {});
            }
            else
            {
                world.Remove<M<7>>(e);
            }
            break;
        }
    };
    for (u32 i = 0; i < 100000; ++i)
    {
        toggle(ids[rng.Below(200)], rng.Below(8));
    }
    const WorldStats stats = world.Stats();
    CHECK(stats.archetypes <= 256);
    CHECK(stats.migrations == 100000);
    CHECK(stats.graph_hits >= 100000 - 256 * 8);
    CHECK(world.Validate().Ok());
}

TEST_CASE("World: CreateMany equals the loop of Create", "[ecs][world]")
{
    World bulk;
    World loop;
    bulk.CreateMany<M<0>, M<1>, WFrozen>(3000,
                                         [](u32 i, M<0>& a, M<1>& b, WFrozen&)
                                         {
                                             a.v = i;
                                             b.v = i * 3;
                                         });
    Vector<Entity> loop_ids;
    for (u32 i = 0; i < 3000; ++i)
    {
        loop_ids.Push(loop.Create(M<0>{i}, M<1>{i * 3}, WFrozen{}));
    }
    CHECK(bulk.Stats().entities == 3000);
    CHECK(bulk.Stats().archetypes == loop.Stats().archetypes);
    CHECK(bulk.Validate().Ok());
    for (u32 i = 0; i < 3000; ++i)
    {
        // creation order is the same, so the i-th entity of both worlds has the same slot
        const Entity e{loop_ids[i].index, loop_ids[i].generation};
        REQUIRE(bulk.Get<M<0>>(e)->v == i);
        REQUIRE(bulk.Get<M<1>>(e)->v == i * 3);
        REQUIRE(loop.Get<M<1>>(e)->v == i * 3);
    }
    CHECK(bulk.ArchetypeAt(1).ChunkCount() == loop.ArchetypeAt(1).ChunkCount());
}

TEST_CASE("World: a structural call during iteration is reported", "[ecs][world]")
{
    World world;
    const Entity e = world.Create(M<0>{1});
    CHECK_FALSE(world.IsIterating());
    const AssertHandler previous = SetAssertHandler(&CountAndContinue);
    g_assert_hits = 0;
    world.BeginIteration();
    CHECK(world.IsIterating());
    world.Create();
    world.Add<M<1>>(e, {});
    world.Remove<M<1>>(e);
    world.Destroy(e);
    CHECK(g_assert_hits == 4);
    world.Set<M<0>>(world.Create(M<0>{}), M<0>{2});  // Create asserts once more; the overwrite is allowed
    world.EndIteration();
    SetAssertHandler(previous);
    CHECK(g_assert_hits == 5);
    CHECK_FALSE(world.IsIterating());
}

TEST_CASE("World: listing a component type twice is reported", "[ecs][world]")
{
    World world;
    const AssertHandler previous = SetAssertHandler(&CountAndContinue);
    g_assert_hits = 0;
    world.Create(M<0>{1}, M<0>{2});
    SetAssertHandler(previous);
    CHECK(g_assert_hits == 1);
}

TEST_CASE("World: Validate reports a broken invariant", "[ecs][world]")
{
    World world;
    const Entity e = world.Create(M<0>{1});
    CHECK(world.Validate().Ok());
    // Corrupt the row's entity id directly, as a bug would.
    Archetype& a = world.ArchetypeAt(1);
    a.Entities(a.ChunkAt(0))[0] = Entity{12345, 1};
    const ValidationReport report = world.Validate();
    CHECK_FALSE(report.Ok());
    CHECK(report.first != nullptr);
    a.Entities(a.ChunkAt(0))[0] = e;
    CHECK(world.Validate().Ok());
}

namespace
{

constexpr u32 kTypes = 6;  // M0, M1, M2, M3 (counted), M4 (aligned), tag

struct Model
{
    Vector<Entity> alive;
    Vector<u8> has[kTypes];
    Vector<u32> val[kTypes];
    void Ensure(u32 index)
    {
        for (u32 k = 0; k < kTypes; ++k)
        {
            while (has[k].Size() <= index)
            {
                has[k].Push(0);
                val[k].Push(0);
            }
        }
    }
};

template <class F> void WithType(u32 k, F&& f)
{
    switch (k)
    {
    case 0: f(M<0>{}); break;
    case 1: f(M<1>{}); break;
    case 2: f(M<2>{}); break;
    case 3: f(M<3>{}); break;
    case 4: f(M<4>{}); break;
    default: f(WFrozen{}); break;
    }
}

template <class T> constexpr u32 ValueOf(const T* p)
{
    if constexpr (IsEmpty<T>)
    {
        return 0;
    }
    else
    {
        return p->v;
    }
}

}  // namespace

TEST_CASE("World: randomized structural changes agree with a model", "[ecs][world]")
{
    for (u32 seed = 1; seed <= 5; ++seed)
    {
        WCounted::live = 0;
        {
            World world;
            Model model;
            ecs_test::Rng rng(seed * 7919u);
            auto verify = [&]
            {
                REQUIRE(world.Validate().Ok());
                REQUIRE(world.Stats().entities == model.alive.Size());
                i64 counted = 0;
                for (usize i = 0; i < model.alive.Size(); ++i)
                {
                    const Entity e = model.alive[i];
                    for (u32 k = 0; k < kTypes; ++k)
                    {
                        WithType(k,
                                 [&](auto proto)
                                 {
                                     using T = decltype(proto);
                                     REQUIRE(world.Has<T>(e) == (model.has[k][e.index] != 0));
                                     if (model.has[k][e.index] != 0 && !IsEmpty<T>)
                                     {
                                         REQUIRE(ValueOf(world.Get<T>(e)) == model.val[k][e.index]);
                                     }
                                 });
                    }
                    counted += model.has[3][e.index];
                }
                REQUIRE(WCounted::live == counted);
            };
            for (u32 op = 1; op <= 100000; ++op)
            {
                const u32 roll = rng.Below(100);
                if (model.alive.Size() == 0 || (roll < 15 && model.alive.Size() < 3000))
                {
                    const Entity e = world.Create();
                    REQUIRE(e.IsValid());
                    model.Ensure(e.index);
                    for (u32 k = 0; k < kTypes; ++k)
                    {
                        model.has[k][e.index] = 0;
                    }
                    model.alive.Push(e);
                    continue;
                }
                const usize pick = rng.Below(static_cast<u32>(model.alive.Size()));
                const Entity e = model.alive[pick];
                const u32 k = rng.Below(kTypes);
                const u32 value = rng.Below(1000000);
                if (roll < 25 || model.alive.Size() >= 3000)
                {
                    REQUIRE(world.Destroy(e) == Status::Ok);
                    for (u32 t = 0; t < kTypes; ++t)
                    {
                        model.has[t][e.index] = 0;
                    }
                    model.alive[pick] = model.alive[model.alive.Size() - 1];
                    model.alive.Pop();
                }
                else if (roll < 55)
                {
                    WithType(k,
                             [&](auto proto)
                             {
                                 using T = decltype(proto);
                                 T v{};
                                 if constexpr (!IsEmpty<T>)
                                 {
                                     v.v = value;
                                 }
                                 const Status s = world.Add<T>(e, Move(v));
                                 REQUIRE(s == (model.has[k][e.index] ? Status::AlreadyHas : Status::Ok));
                                 if (s == Status::Ok)
                                 {
                                     model.has[k][e.index] = 1;
                                     model.val[k][e.index] = value;
                                 }
                             });
                }
                else if (roll < 80)
                {
                    WithType(k,
                             [&](auto proto)
                             {
                                 using T = decltype(proto);
                                 const Status s = world.Remove<T>(e);
                                 REQUIRE(s == (model.has[k][e.index] ? Status::Ok : Status::MissingComponent));
                                 model.has[k][e.index] = 0;
                             });
                }
                else
                {
                    WithType(k,
                             [&](auto proto)
                             {
                                 using T = decltype(proto);
                                 T v{};
                                 if constexpr (!IsEmpty<T>)
                                 {
                                     v.v = value;
                                 }
                                 REQUIRE(world.Set<T>(e, Move(v)) == Status::Ok);
                                 model.has[k][e.index] = 1;
                                 model.val[k][e.index] = value;
                             });
                }
                if (op % 500 == 0)
                {
                    verify();
                }
            }
            verify();
        }
        CHECK(WCounted::live == 0);
    }
}
