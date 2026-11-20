// The hierarchy's children index (query and hierarchy indices series, Day 2): SetParent, ClearParent and
// DestroyRecursive over the index give the same statuses, depths, storage order and command order as the old pass over
// every Parent link (kept below as the oracle), over random histories that include raw writes, removals and
// destruction; and the world's component versions move on every path that can change a component.
#include <catch2/catch_test_macros.hpp>

#include "containers/hash_map.h"
#include "containers/vector.h"
#include "cstr/mem_ops.h"
#include "ecs/command_buffer.h"
#include "ecs/query.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "mem/heap_allocator.h"
#include "test_rng.h"

using namespace vse;
using namespace vse::ecs;

namespace
{

// The hierarchy calls as they were before the index: a pass over every Parent link per call.
namespace oracle
{

struct Link
{
    Entity entity;
    Entity parent;
};

void CollectLinks(World& world, Vector<Link>* out)
{
    world.Query<Read<Parent>>().ForEach([&](Entity e, const Parent& p) { out->Push(Link{e, p.entity}); });
}

void Descendants(const Vector<Link>& links, Entity root, HashMap<Entity, u32, HeapAllocator>* distance, u32* height)
{
    distance->Insert(root, 0);
    *height = 0;
    for (bool grew = true; grew;)
    {
        grew = false;
        for (usize i = 0; i < links.Size(); ++i)
        {
            const u32* up = distance->Find(links[i].parent);
            if (up != nullptr && distance->Find(links[i].entity) == nullptr)
            {
                const u32 next = *up + 1;
                distance->Insert(links[i].entity, next);
                *height = next > *height ? next : *height;
                grew = true;
            }
        }
    }
}

void RebaseDepths(World& world, const Vector<Link>& links, Entity root, u32 root_depth)
{
    HashMap<Entity, u32, HeapAllocator> distance(HeapAllocator::Default());
    u32 height = 0;
    Descendants(links, root, &distance, &height);
    for (usize i = 0; i < links.Size(); ++i)
    {
        const u32* d = distance.Find(links[i].entity);
        if (d != nullptr && links[i].entity != root)
        {
            world.Set<HierarchyDepth>(links[i].entity, HierarchyDepth{root_depth + *d});
        }
    }
}

u32 SubtreeHeight(const Vector<Link>& links, Entity root)
{
    HashMap<Entity, u32, HeapAllocator> distance(HeapAllocator::Default());
    u32 height = 0;
    Descendants(links, root, &distance, &height);
    return height;
}

HierarchyStatus SetParent(World& world, Entity child, Entity parent)
{
    if (!world.Exists(child) || !world.Exists(parent))
    {
        return HierarchyStatus::DeadEntity;
    }
    if (child == parent)
    {
        return HierarchyStatus::SelfParent;
    }
    u32 parent_depth = 0;
    for (Entity up = parent;;)
    {
        const Parent* above = world.Peek<Parent>(up);
        if (above == nullptr)
        {
            break;
        }
        up = above->entity;
        ++parent_depth;
        if (up == child)
        {
            return HierarchyStatus::Cycle;
        }
        if (parent_depth > kMaxHierarchyDepth)
        {
            break;
        }
    }
    Vector<Link> links;
    CollectLinks(world, &links);
    if (parent_depth + 1 + SubtreeHeight(links, child) > kMaxHierarchyDepth)
    {
        if (TransformReport* report = world.TryResource<TransformReport>())
        {
            ++report->refused_depth;
        }
        return HierarchyStatus::TooDeep;
    }
    const bool had_parent = world.Has<Parent>(child);
    if (world.Set<Parent>(child, Parent{parent}) != Status::Ok)
    {
        return HierarchyStatus::Refused;
    }
    if (world.Set<HierarchyDepth>(child, HierarchyDepth{parent_depth + 1}) != Status::Ok)
    {
        if (!had_parent)
        {
            world.Remove<Parent>(child);
        }
        return HierarchyStatus::Refused;
    }
    RebaseDepths(world, links, child, parent_depth + 1);
    return HierarchyStatus::Ok;
}

HierarchyStatus ClearParent(World& world, Entity child)
{
    if (!world.Exists(child))
    {
        return HierarchyStatus::DeadEntity;
    }
    if (!world.Has<Parent>(child))
    {
        return HierarchyStatus::Ok;
    }
    world.Remove<Parent>(child);
    world.Remove<HierarchyDepth>(child);
    Vector<Link> links;
    CollectLinks(world, &links);
    RebaseDepths(world, links, child, 0);
    return HierarchyStatus::Ok;
}

u32 DestroyRecursive(World& world, CommandBuffer& commands, Entity root)
{
    Vector<Link> links;
    CollectLinks(world, &links);
    HashMap<Entity, u32, HeapAllocator> distance(HeapAllocator::Default());
    u32 height = 0;
    Descendants(links, root, &distance, &height);
    u32 recorded = 0;
    for (u32 d = height + 1; d > 0; --d)
    {
        for (usize i = 0; i < links.Size(); ++i)
        {
            const u32* at = distance.Find(links[i].entity);
            if (at != nullptr && *at == d - 1 && links[i].entity != root)
            {
                commands.Destroy(links[i].entity);
                ++recorded;
            }
        }
    }
    commands.Destroy(root);
    return recorded + 1;
}

}  // namespace oracle

// A world with the report and the propagation, as the transform tests make it.
struct Side
{
    World world;
    Schedule schedule;
    Side()
    {
        world.SetResource(TransformReport{});
        schedule.Add(MakeTransformSystem("transforms"));
        REQUIRE(schedule.Build().ok);
    }
    void Frame() { schedule.Run(world, 0.016f); }
};

LocalTransform At(f32 x)
{
    LocalTransform t;
    t.position = Vec3(x, 0.5f * x, 1.0f);
    return t;
}

bool SameMatrix(const Mat4& a, const Mat4& b)
{
    return MemCompare(&a, &b, sizeof(Mat4)) == 0;
}

// Whether the two worlds agree on every entity of `made`: alive, parent, depth, place in storage and world matrix.
bool Agree(Side& a, Side& b, const Vector<Entity>& made, u32* where)
{
    for (usize i = 0; i < made.Size(); ++i)
    {
        const Entity e = made[i];
        *where = static_cast<u32>(i);
        if (a.world.Exists(e) != b.world.Exists(e))
        {
            return false;
        }
        if (!a.world.Exists(e))
        {
            continue;
        }
        if (a.world.StorageOrder(e) != b.world.StorageOrder(e))
        {
            return false;
        }
        const Parent* pa = a.world.Peek<Parent>(e);
        const Parent* pb = b.world.Peek<Parent>(e);
        if ((pa == nullptr) != (pb == nullptr) || (pa != nullptr && !(pa->entity == pb->entity)))
        {
            return false;
        }
        const HierarchyDepth* da = a.world.Peek<HierarchyDepth>(e);
        const HierarchyDepth* db = b.world.Peek<HierarchyDepth>(e);
        if ((da == nullptr) != (db == nullptr) || (da != nullptr && da->depth != db->depth))
        {
            return false;
        }
        const WorldTransform* wa = a.world.Peek<WorldTransform>(e);
        const WorldTransform* wb = b.world.Peek<WorldTransform>(e);
        if ((wa == nullptr) != (wb == nullptr) || (wa != nullptr && !SameMatrix(wa->matrix, wb->matrix)))
        {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("Hierarchy index: SetParent, ClearParent and DestroyRecursive equal the link pass over random histories",
          "[ecs][transform][hierarchy_index]")
{
    for (u64 seed = 1; seed <= 6; ++seed)
    {
        ecs_test::Rng rng(seed * 7919);
        Side fast;  // the calls over the index
        Side slow;  // the oracle
        Vector<Entity> made;
        u32 statuses_differ = 0;
        u32 counts_differ = 0;
        const auto create = [&]
        {
            const LocalTransform local = At(static_cast<f32>(made.Size()));
            const Entity ea = CreateTransformEntity(fast.world, local);
            const Entity eb = CreateTransformEntity(slow.world, local);
            REQUIRE(ea == eb);
            made.Push(ea);
        };
        for (u32 i = 0; i < 40; ++i)
        {
            create();
        }
        for (u32 step = 0; step < 1500; ++step)
        {
            const Entity x = made[rng.Below(static_cast<u32>(made.Size()))];
            const Entity y = made[rng.Below(static_cast<u32>(made.Size()))];
            const u32 what = rng.Below(100);
            if (what < 45)
            {
                statuses_differ += SetParent(fast.world, x, y) != oracle::SetParent(slow.world, x, y) ? 1 : 0;
            }
            else if (what < 55)
            {
                statuses_differ += ClearParent(fast.world, x) != oracle::ClearParent(slow.world, x) ? 1 : 0;
            }
            else if (what < 62)
            {
                if (fast.world.Exists(x))
                {
                    CommandBuffer ca;
                    CommandBuffer cb;
                    counts_differ += DestroyRecursive(fast.world, ca, x) != oracle::DestroyRecursive(slow.world, cb, x)
                                         ? 1
                                         : 0;
                    fast.world.Apply(ca);
                    slow.world.Apply(cb);
                }
            }
            else if (what < 67)
            {
                fast.world.Destroy(x);  // a parent destroyed alone leaves its children's links dangling
                slow.world.Destroy(x);
            }
            else if (what < 71)
            {
                if (fast.world.Exists(x) && fast.world.Exists(y) && !(x == y))
                {
                    fast.world.Set<Parent>(x, Parent{y});  // a raw write, maybe a cycle: the propagation repairs it
                    slow.world.Set<Parent>(x, Parent{y});
                }
            }
            else if (what < 74)
            {
                fast.world.Remove<Parent>(x);
                slow.world.Remove<Parent>(x);
            }
            else if (what < 77)
            {
                if (Parent* p = fast.world.Get<Parent>(x))
                {
                    p->entity = y;  // a write through Get
                    slow.world.Get<Parent>(x)->entity = y;
                }
            }
            else if (what < 90)
            {
                create();
            }
            else
            {
                fast.Frame();
                slow.Frame();
            }
            u32 where = 0;
            INFO("seed " << seed << ", step " << step << ", entity " << where);
            REQUIRE(Agree(fast, slow, made, &where));
            // The same entity is made next on both sides (the destroy order reached the free lists alike).
        }
        CHECK(statuses_differ == 0);
        CHECK(counts_differ == 0);
        // The index followed the calls without a rebuild each time.
        const TransformReport& report = fast.world.Resource<TransformReport>();
        INFO("index rebuilds " << report.index_rebuilds);
        CHECK(report.index_rebuilds > 0);
        CHECK(report.index_rebuilds < 1000);
    }
}

TEST_CASE("Hierarchy index: building a hierarchy by SetParent makes the index once",
          "[ecs][transform][hierarchy_index]")
{
    World world;
    world.SetResource(TransformReport{});
    Vector<Entity> made;
    for (u32 i = 0; i < 2000; ++i)
    {
        made.Push(CreateTransformEntity(world, At(static_cast<f32>(i))));
    }
    for (u32 i = 1; i < 2000; ++i)
    {
        REQUIRE(SetParent(world, made[i], made[(i - 1) / 8]) == HierarchyStatus::Ok);
    }
    CHECK(world.Resource<TransformReport>().index_rebuilds == 1);
    // Reparenting a subtree rebases its depths through the index.
    REQUIRE(SetParent(world, made[1], made[1999]) == HierarchyStatus::Ok);
    CHECK(world.Peek<HierarchyDepth>(made[9])->depth == world.Peek<HierarchyDepth>(made[1999])->depth + 2);
    REQUIRE(ClearParent(world, made[1]) == HierarchyStatus::Ok);
    CHECK(world.Peek<HierarchyDepth>(made[9])->depth == 1);
    CHECK(world.Resource<TransformReport>().index_rebuilds == 1);
    // A raw write moves the version: the next call makes the index again, once.
    world.Set<Parent>(made[5], Parent{made[1]});
    REQUIRE(SetParent(world, made[6], made[1]) == HierarchyStatus::Ok);
    REQUIRE(SetParent(world, made[7], made[1]) == HierarchyStatus::Ok);
    CHECK(world.Resource<TransformReport>().index_rebuilds == 2);
}

TEST_CASE("World: a tracked component's version moves on every path that can change it",
          "[ecs][world][component_version]")
{
    World world;
    CHECK(world.ComponentVersion<Parent>() == 0);  // not tracked
    world.TrackComponentVersion<Parent>();
    world.TrackComponentVersion<Parent>();  // again: nothing changes
    u64 last = world.ComponentVersion<Parent>();
    const auto moved = [&]
    {
        const u64 now = world.ComponentVersion<Parent>();
        const bool grew = now > last;
        last = now;
        return grew;
    };
    const Entity a = world.Create(LocalTransform{});
    const Entity b = world.Create(LocalTransform{});
    CHECK(!moved());  // no Parent anywhere
    REQUIRE(world.Add<Parent>(a, Parent{b}) == Status::Ok);
    CHECK(moved());
    world.Set<Parent>(a, Parent{b});
    CHECK(moved());
    world.Get<Parent>(a);
    CHECK(moved());
    world.Peek<Parent>(a);
    CHECK(!moved());  // a read
    world.Poke<Parent>(a);
    CHECK(moved());
    world.Stamp<Parent>(a);
    CHECK(moved());
    world.CellOf(a, ComponentOf<Parent>());
    CHECK(moved());
    world.Add<HierarchyDepth>(a, HierarchyDepth{1});  // a move that keeps Parent's value
    CHECK(!moved());
    world.Query<Write<Parent>>().ForEach([](Parent&) {});
    CHECK(moved());
    world.Query<Read<Parent>>().ForEach([](const Parent&) {});
    CHECK(!moved());
    const Entity c = world.Create(LocalTransform{}, Parent{b});
    CHECK(moved());
    REQUIRE(world.Remove<Parent>(c) == Status::Ok);
    CHECK(moved());
    REQUIRE(world.Destroy(a) == Status::Ok);  // the last row of its archetype: no swap, still counted
    CHECK(moved());
    // An untracked type stays at 0 whatever happens to it.
    world.Set<LocalTransform>(b, LocalTransform{});
    CHECK(world.ComponentVersion<LocalTransform>() == 0);
}
