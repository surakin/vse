#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "ecs/component.h"
#include "ecs/component_set.h"
#include "sync/atomic.h"
#include "sync/thread.h"
#include "cstr/cstr.h"
#include "lang/new.h"
#include "test_rng.h"

using namespace vse;
using namespace vse::ecs;

namespace
{

struct Pos
{
    float x, y, z;
};
struct Marker
{
};
struct Named
{
    static constexpr const char* kComponentName = "Named";
    int v = 0;
};
struct Counted
{
    static inline int live = 0;
    static inline int moves = 0;
    int v = 0;
    Counted() { ++live; }
    Counted(Counted&& o) noexcept : v(o.v)
    {
        ++live;
        ++moves;
    }
    Counted(const Counted&) = delete;
    ~Counted() { --live; }
};
struct NoDefault
{
    explicit NoDefault(int) {}
};
struct alignas(64) Wide
{
    char c[64];
};

template <int N> struct Numbered
{
    int v = N;
};

TypeInfo InfoOf(ComponentId id)
{
    return ComponentRegistry::Global().Info(id);
}

}  // namespace

TEST_CASE("ComponentOf: one stable id per type, distinct across types", "[ecs][component]")
{
    const ComponentId a = ComponentOf<Pos>();
    const ComponentId b = ComponentOf<Marker>();
    CHECK(a != kInvalidComponent);
    CHECK(b != kInvalidComponent);
    CHECK(a != b);
    CHECK(ComponentOf<Pos>() == a);
    CHECK(ComponentOf<Marker>() == b);
    CHECK(a < ComponentRegistry::Global().Count());
}

TEST_CASE("TypeInfo: a trivial type, a tag, a named type and a wide type", "[ecs][component]")
{
    const TypeInfo pos = InfoOf(ComponentOf<Pos>());
    CHECK(pos.size == sizeof(Pos));
    CHECK(pos.align == alignof(Pos));
    CHECK_FALSE(pos.is_tag);
    CHECK(pos.trivially_relocatable);
    CHECK(pos.default_construct != nullptr);

    const TypeInfo tag = InfoOf(ComponentOf<Marker>());
    CHECK(tag.is_tag);
    CHECK(tag.size == 0);
    CHECK(tag.align == 1);

    CHECK(CStrCompare(InfoOf(ComponentOf<Named>()).name, "Named") == 0);
    const char* generated = InfoOf(ComponentOf<Pos>()).name;
    CHECK(generated[0] == 'c');

    const TypeInfo wide = InfoOf(ComponentOf<Wide>());
    CHECK(wide.align == 64);
    CHECK(InfoOf(ComponentOf<NoDefault>()).default_construct == nullptr);
}

TEST_CASE("TypeInfo: a non-trivial type moves and destroys through its functions", "[ecs][component]")
{
    const TypeInfo info = InfoOf(ComponentOf<Counted>());
    CHECK_FALSE(info.trivially_relocatable);
    alignas(Counted) byte a[sizeof(Counted)];
    alignas(Counted) byte b[sizeof(Counted)];
    Counted::live = 0;
    Counted::moves = 0;
    ConstructAt<Counted>(a);
    reinterpret_cast<Counted*>(a)->v = 42;
    CHECK(Counted::live == 1);
    info.move_construct(b, a);
    CHECK(Counted::live == 2);
    CHECK(Counted::moves == 1);
    CHECK(reinterpret_cast<Counted*>(b)->v == 42);
    info.destroy(a);
    info.destroy(b);
    CHECK(Counted::live == 0);
}

TEST_CASE("ComponentRegistry: the cap refuses and counts, and the refused type keeps the same answer",
          "[ecs][component]")
{
    ComponentRegistry registry;
    TypeInfo info;
    info.size = 4;
    info.align = 4;
    for (u32 i = 0; i < kMaxComponentTypes; ++i)
    {
        REQUIRE(registry.Register(info) == i);
    }
    CHECK(registry.Count() == kMaxComponentTypes);
    CHECK(registry.Refused() == 0);
    CHECK(registry.Register(info) == kInvalidComponent);
    CHECK(registry.Register(info) == kInvalidComponent);
    CHECK(registry.Refused() == 2);
    CHECK(registry.Count() == kMaxComponentTypes);
    CHECK(CStrCompare(registry.Info(127).name, "component#127") == 0);
    CHECK(CStrCompare(registry.Info(0).name, "component#0") == 0);
}

TEST_CASE("ComponentOf: 8 threads registering the same 8 types in different orders agree on every id",
          "[ecs][component]")
{
    constexpr u32 kThreads = 8;
    Atomic<u32> start{0};
    ComponentId seen[kThreads][8];
    Vector<Thread> threads;
    for (u32 t = 0; t < kThreads; ++t)
    {
        threads.Push(Thread(
            [t, &start, &seen]
            {
                while (start.Load() == 0)
                {
                }
                // each thread asks for the same eight types, starting at a different one
                for (u32 k = 0; k < 8; ++k)
                {
                    const u32 which = (k + t) % 8;
                    ComponentId id = kInvalidComponent;
                    switch (which)
                    {
                    case 0: id = ComponentOf<Numbered<0>>(); break;
                    case 1: id = ComponentOf<Numbered<1>>(); break;
                    case 2: id = ComponentOf<Numbered<2>>(); break;
                    case 3: id = ComponentOf<Numbered<3>>(); break;
                    case 4: id = ComponentOf<Numbered<4>>(); break;
                    case 5: id = ComponentOf<Numbered<5>>(); break;
                    case 6: id = ComponentOf<Numbered<6>>(); break;
                    default: id = ComponentOf<Numbered<7>>(); break;
                    }
                    seen[t][which] = id;
                }
            }));
    }
    start.Store(1);
    for (usize ti = 0; ti < threads.Size(); ++ti)
    {
        Thread& th = threads[ti];
        th.Join();
    }
    for (u32 which = 0; which < 8; ++which)
    {
        for (u32 t = 1; t < kThreads; ++t)
        {
            CHECK(seen[t][which] == seen[0][which]);
        }
        for (u32 other = which + 1; other < 8; ++other)
        {
            CHECK(seen[0][which] != seen[0][other]);
        }
    }
}

TEST_CASE("ComponentRegistry: concurrent registration hands out distinct ids", "[ecs][component]")
{
    ComponentRegistry registry;
    constexpr u32 kThreads = 8;
    constexpr u32 kEach = 16;
    ComponentId ids[kThreads][kEach];
    Vector<Thread> threads;
    for (u32 t = 0; t < kThreads; ++t)
    {
        threads.Push(Thread(
            [t, &registry, &ids]
            {
                TypeInfo info;
                info.size = 4;
                info.align = 4;
                for (u32 k = 0; k < kEach; ++k)
                {
                    ids[t][k] = registry.Register(info);
                    (void)registry.Info(ids[t][k]);
                }
            }));
    }
    for (usize ti = 0; ti < threads.Size(); ++ti)
    {
        Thread& th = threads[ti];
        th.Join();
    }
    bool used[kMaxComponentTypes] = {};
    for (u32 t = 0; t < kThreads; ++t)
    {
        for (u32 k = 0; k < kEach; ++k)
        {
            REQUIRE(ids[t][k] < kMaxComponentTypes);
            CHECK_FALSE(used[ids[t][k]]);
            used[ids[t][k]] = true;
        }
    }
    CHECK(registry.Count() == kThreads * kEach);
}

TEST_CASE("ComponentSet: matches a reference bool vector under random operations", "[ecs][component]")
{
    ecs_test::Rng rng(0xC0FFEEu);
    for (u32 round = 0; round < 50; ++round)
    {
        ComponentSet set;
        ComponentSet other;
        bool ref[kMaxComponentTypes] = {};
        bool ref_other[kMaxComponentTypes] = {};
        for (u32 step = 0; step < 300; ++step)
        {
            const ComponentId id = static_cast<ComponentId>(rng.Below(kMaxComponentTypes));
            switch (rng.Below(4))
            {
            case 0:
                set.Add(id);
                ref[id] = true;
                break;
            case 1:
                set.Remove(id);
                ref[id] = false;
                break;
            case 2:
                other.Add(id);
                ref_other[id] = true;
                break;
            default:
                other.Remove(id);
                ref_other[id] = false;
                break;
            }
        }
        u32 count = 0;
        bool subset = true;
        bool intersects = false;
        for (u32 i = 0; i < kMaxComponentTypes; ++i)
        {
            REQUIRE(set.Has(static_cast<ComponentId>(i)) == ref[i]);
            count += ref[i] ? 1u : 0u;
            if (ref[i] && !ref_other[i])
            {
                subset = false;
            }
            if (ref[i] && ref_other[i])
            {
                intersects = true;
            }
        }
        CHECK(set.Count() == count);
        CHECK(set.IsEmpty() == (count == 0));
        CHECK(set.IsSubsetOf(other) == subset);
        CHECK(set.Intersects(other) == intersects);
        u32 visited = 0;
        i32 last = -1;
        for (ComponentId id = set.NextFrom(0); id != kInvalidComponent; id = set.NextFrom(static_cast<u32>(id) + 1))
        {
            CHECK(static_cast<i32>(id) > last);
            CHECK(ref[id]);
            last = id;
            ++visited;
        }
        CHECK(visited == count);
    }
}

TEST_CASE("ComponentSet: equal sets hash equally, edge ids work, out of range is absent", "[ecs][component]")
{
    ComponentSet a;
    ComponentSet b;
    a.Add(0);
    a.Add(63);
    a.Add(64);
    a.Add(127);
    b.Add(127);
    b.Add(64);
    b.Add(63);
    b.Add(0);
    CHECK(a == b);
    CHECK(a.Hash() == b.Hash());
    b.Remove(63);
    CHECK(a != b);
    CHECK(a.Count() == 4);
    CHECK(a.NextFrom(1) == 63);
    CHECK(a.NextFrom(65) == 127);
    CHECK(a.NextFrom(128) == kInvalidComponent);
    CHECK_FALSE(a.Has(128));
    CHECK_FALSE(a.Has(kInvalidComponent));
    CHECK(ComponentSet{}.IsEmpty());
}
