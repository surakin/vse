// The spatial sync (Phase 18 Day 245): after every frame the sets equal a brute-force rebuild from the entities (200
// random frames of moves, spawns, destroys and reparenting); a static scene costs no structure calls; the parallel path
// equals the sequential one; moving a parent moves its children's items; the cost of the sync for 100,000 entities with
// 1, 10 and 100 percent moving is recorded.
#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "jobs/scheduler.h"
#include "lang/limits.h"
#include "os/os.h"
#include "spatial/components.h"
#include "spatial/query.h"
#include "spatial/spatial_sync.h"
#include "spatial/spatial_world.h"
#include "spatial_test_rng.h"
#include "spatial_test_util.h"

using namespace vse;
using namespace vse::spatial;
using spatial_test::BoxAt;
using spatial_test::Rng;

namespace
{

struct SyItem
{
    SpatialKey key = 0;
    Aabb box;
};

bool SyBoxSame(const Aabb& a, const Aabb& b)
{
    return a.min.x == b.min.x && a.min.y == b.min.y && a.min.z == b.min.z && a.max.x == b.max.x && a.max.y == b.max.y &&
           a.max.z == b.max.z;
}

struct SyRig
{
    ecs::World world;
    ecs::Schedule schedule;
    Vector<u64> static_keys;  // a tag has no Get, so the rig remembers which entities are static

    explicit SyRig(jobs::Scheduler* scheduler = nullptr)
    {
        world.SetResource(ecs::TransformReport{});
        world.SetResource(SpatialWorld());
        schedule.Add(ecs::MakeTransformSystem("transforms"));
        schedule.Add(MakeSpatialSyncSystem("spatial_sync").InStage(ecs::kPostUpdate));
        schedule.SetScheduler(scheduler);
        REQUIRE(schedule.Build().ok);
    }
    void Frame() { schedule.Run(world, 0.016f); }
    SpatialWorld& Spatial() { return world.Resource<SpatialWorld>(); }
    bool IsStatic(ecs::Entity e) const
    {
        for (usize i = 0; i < static_keys.Size(); ++i)
        {
            if (static_keys[i] == e.ToU64())
            {
                return true;
            }
        }
        return false;
    }
    ecs::Entity Spawn(const Vec3& at, bool is_static = false, u32 sets = 3)
    {
        ecs::LocalTransform local;
        local.position = at;
        SpatialBounds bounds;
        bounds.local = BoxAt(Vec3(0, 0, 0), Vec3(0.5f, 0.5f, 0.5f));
        SpatialMembership membership;
        membership.sets = static_cast<u8>(sets);
        const ecs::Entity e = world.Create(local, ecs::WorldTransform{}, bounds, membership);
        if (is_static)
        {
            world.Add<SpatialStatic>(e, SpatialStatic{});
            static_keys.Push(e.ToU64());
        }
        return e;
    }
    // Every (key, box) in a set, sorted by key.
    Vector<SyItem> Items(SetId id)
    {
        Vector<SyItem> items;
        const SpatialSet& set = *Spatial().Set(id);
        Overlap(set, BoxAt(Vec3(0, 0, 0), Vec3(1e6f, 1e6f, 1e6f)), QueryFilter(),
                [&](const Item& item)
                {
                    SyItem s;
                    s.key = item.key;
                    s.box = item.box;
                    items.Push(s);
                    return true;
                });
        for (usize i = 1; i < items.Size(); ++i)
        {
            for (usize j = i; j > 0 && items[j].key < items[j - 1].key; --j)
            {
                const SyItem tmp = items[j];
                items[j] = items[j - 1];
                items[j - 1] = tmp;
            }
        }
        return items;
    }
    // The sets the entities say they should be: every entity with the three components, box from its WorldTransform.
    bool Matches(const Vector<ecs::Entity>& live)
    {
        for (SetId id = 0; id < 2; ++id)
        {
            Vector<SyItem> have = Items(id);
            Vector<SyItem> want;
            for (usize i = 0; i < live.Size(); ++i)
            {
                const SpatialMembership* m = world.Get<SpatialMembership>(live[i]);
                if (((m->sets >> id) & 1u) == 0)
                {
                    continue;
                }
                SyItem s;
                s.key = live[i].ToU64();
                s.box = TransformAabb(world.Get<SpatialBounds>(live[i])->local,
                                      world.Get<ecs::WorldTransform>(live[i])->matrix);
                want.Push(s);
            }
            for (usize i = 1; i < want.Size(); ++i)
            {
                for (usize j = i; j > 0 && want[j].key < want[j - 1].key; --j)
                {
                    const SyItem tmp = want[j];
                    want[j] = want[j - 1];
                    want[j - 1] = tmp;
                }
            }
            if (have.Size() != want.Size())
            {
                UNSCOPED_INFO("set " << id << " have " << have.Size() << " want " << want.Size());
                return false;
            }
            for (usize i = 0; i < have.Size(); ++i)
            {
                if (have[i].key != want[i].key || !SyBoxSame(have[i].box, want[i].box))
                {
                    UNSCOPED_INFO("set " << id << " item " << i << " key " << have[i].key << " vs " << want[i].key
                                         << " have x " << have[i].box.min.x << " want x " << want[i].box.min.x);
                    return false;
                }
            }
        }
        return true;
    }
};

jobs::Scheduler::Config SyConfig(usize participants)
{
    jobs::Scheduler::Config config;
    config.participants = participants;
    config.pin = false;
    return config;
}

// Runs a scripted scene for `frames` frames and leaves the entities behind; the same seed gives the same script.
void SyRunScript(SyRig& rig, Vector<ecs::Entity>* live, u64 seed, u32 frames, bool check)
{
    Rng rng(seed);
    for (u32 frame = 0; frame < frames; ++frame)
    {
        for (u32 n = rng.Below(6); n > 0; --n)
        {
            live->Push(rig.Spawn(rng.Point(-50, 50), rng.Below(5) == 0, 1 + rng.Below(3)));
        }
        for (u32 n = 0; n < 12 && live->Size() > 0; ++n)
        {
            const usize i = rng.Below(static_cast<u32>(live->Size()));
            const ecs::Entity e = (*live)[i];
            const u32 op = rng.Below(10);
            if (op < 6)
            {
                if (!rig.IsStatic(e))
                {  // static items do not move
                    rig.world.Get<ecs::LocalTransform>(e)->position = rng.Point(-50, 50);
                }
            }
            else if (op < 7)
            {
                rig.world.Destroy(e);
                (*live)[i] = (*live)[live->Size() - 1];
                live->Pop();
            }
            else if (op < 9 && live->Size() > 1)
            {
                const ecs::Entity parent = (*live)[rng.Below(static_cast<u32>(live->Size()))];
                if (parent != e && !rig.IsStatic(e) && !rig.IsStatic(parent))
                {
                    ecs::SetParent(rig.world, e, parent);  // may refuse a cycle
                }
            }
            else if (!rig.IsStatic(e))
            {
                SpatialBounds b;
                b.local = BoxAt(Vec3(0, 0, 0), rng.Point(0.2f, 1.5f));
                rig.world.Get<SpatialBounds>(e)->local = b.local;
            }
        }
        rig.Frame();
        if (check)
        {
            rig.Frame();  // the proxies and bounds of this frame's joiners have landed
            REQUIRE(rig.Matches(*live));
        }
    }
}

}  // namespace

TEST_CASE("Spatial sync: after 200 random frames the sets equal a brute-force rebuild from the entities",
          "[spatial][sync]")
{
    SyRig rig;
    Vector<ecs::Entity> live;
    SyRunScript(rig, &live, 0xC0FFEEull, 200, true);
    CHECK(rig.Spatial().Stats().items_moved > 0);
}

TEST_CASE("Spatial sync: moving a parent moves every child's item", "[spatial][sync]")
{
    SyRig rig;
    const ecs::Entity parent = rig.Spawn(Vec3(10, 0, 0));
    const ecs::Entity kid = rig.Spawn(Vec3(1, 0, 0));
    const ecs::Entity grandkid = rig.Spawn(Vec3(0, 1, 0));
    REQUIRE(ecs::SetParent(rig.world, kid, parent) == ecs::HierarchyStatus::Ok);
    REQUIRE(ecs::SetParent(rig.world, grandkid, kid) == ecs::HierarchyStatus::Ok);
    for (u32 i = 0; i < 4; ++i)
    {
        rig.Frame();
    }
    rig.world.Get<ecs::LocalTransform>(parent)->position = Vec3(30, 0, 0);
    for (u32 i = 0; i < 2; ++i)
    {
        rig.Frame();
    }
    Vector<SyItem> items = rig.Items(kRenderSet);
    REQUIRE(items.Size() == 3);
    for (usize i = 0; i < items.Size(); ++i)
    {
        const ecs::Entity e = ecs::Entity::FromU64(items[i].key);
        CHECK(items[i].box.Center().x == rig.world.Get<ecs::WorldTransform>(e)->matrix.col[3].x);
    }
    CHECK(rig.world.Get<ecs::WorldTransform>(grandkid)->matrix.col[3].x == 31.0f);
}

TEST_CASE("Spatial sync: a static scene costs no structure calls", "[spatial][sync]")
{
    SyRig rig;
    for (u32 i = 0; i < 300; ++i)
    {
        rig.Spawn(Vec3(static_cast<f32>(i), 0, 0), i % 3 == 0);
    }
    for (u32 i = 0; i < 4; ++i)
    {
        rig.Frame();
    }
    const SpatialWorldStats before = rig.Spatial().Stats();
    const u64 rebuilds = rig.Spatial().Set(kRenderSet)->Stats().static_rebuilds;
    for (u32 i = 0; i < 100; ++i)
    {
        rig.Frame();
    }
    const SpatialWorldStats after = rig.Spatial().Stats();
    CHECK(after.items_added == before.items_added);
    CHECK(after.items_removed == before.items_removed);
    CHECK(after.items_moved == before.items_moved);
    CHECK(after.bounds_written == before.bounds_written);
    CHECK(after.static_rebuilds == before.static_rebuilds);
    CHECK(rig.Spatial().Set(kRenderSet)->Stats().static_rebuilds == rebuilds);
    CHECK(rebuilds >= 1);
}

TEST_CASE("Spatial sync: removing a component in the frame the entity moved leaves no item", "[spatial][sync]")
{
    SyRig rig;
    const ecs::Entity e = rig.Spawn(Vec3(1, 0, 0));
    for (u32 i = 0; i < 3; ++i)
    {
        rig.Frame();
    }
    rig.world.Get<ecs::LocalTransform>(e)->position = Vec3(9, 0, 0);
    REQUIRE(rig.world.Remove<SpatialMembership>(e) == ecs::Status::Ok);
    for (u32 i = 0; i < 3; ++i)
    {
        rig.Frame();
    }
    CHECK(rig.Items(kRenderSet).Size() == 0);
    CHECK(rig.Items(kPhysicsSet).Size() == 0);
}

TEST_CASE("Spatial sync: the parallel path gives the same sets as the sequential one", "[spatial][sync]")
{
    jobs::Scheduler jobs(SyConfig(4));
    SyRig sequential;
    sequential.Spatial().SetParallel(false);
    SyRig parallel(&jobs);
    Vector<ecs::Entity> a, b;
    SyRunScript(sequential, &a, 0xABCDEFull, 120, false);
    SyRunScript(parallel, &b, 0xABCDEFull, 120, false);
    for (u32 i = 0; i < 3; ++i)
    {
        sequential.Frame();
        parallel.Frame();
    }
    REQUIRE(a.Size() == b.Size());
    for (SetId id = 0; id < 2; ++id)
    {
        Vector<SyItem> x = sequential.Items(id);
        Vector<SyItem> y = parallel.Items(id);
        REQUIRE(x.Size() == y.Size());
        for (usize i = 0; i < x.Size(); ++i)
        {
            REQUIRE(x[i].key == y[i].key);
            REQUIRE(SyBoxSame(x[i].box, y[i].box));
        }
    }
    CHECK(sequential.Matches(a));
    CHECK(parallel.Matches(b));
    CHECK(sequential.Spatial().Stats().items_moved == parallel.Spatial().Stats().items_moved);
    CHECK(sequential.Spatial().Stats().items_reinserted == parallel.Spatial().Stats().items_reinserted);
    CHECK(parallel.Spatial().Stats().items_moved > 0);  // the script moves items, so the per-set pass had work
}

TEST_CASE("Spatial sync: the cost for 100,000 entities with 1, 10 and 100 percent moving is recorded",
          "[spatial][sync][benchmark]")
{
    constexpr u32 kEntities = 100000;
    SyRig rig;
    Rng rng(7);
    Vector<ecs::Entity> all;
    for (u32 i = 0; i < kEntities; ++i)
    {
        all.Push(rig.Spawn(rng.Point(-500, 500)));
    }
    u64 start = os::MonotonicTimeNanos();
    rig.Frame();
    rig.Frame();
    const f64 build_ms = static_cast<f64>(os::MonotonicTimeNanos() - start) / 1e6;
    const u32 percents[3] = {1, 10, 100};
    f64 ms[3] = {0, 0, 0};
    for (u32 k = 0; k < 3; ++k)
    {
        for (u32 frame = 0; frame < 3; ++frame)
        {
            for (u32 i = 0; i < kEntities / 100 * percents[k]; ++i)
            {
                ecs::LocalTransform* local = rig.world.Get<ecs::LocalTransform>(all[i]);
                local->position = local->position + rng.Point(-1.5f, 1.5f);  // agents walk, they do not teleport
            }
            start = os::MonotonicTimeNanos();
            rig.Frame();
            ms[k] = static_cast<f64>(os::MonotonicTimeNanos() - start) / 1e6;
        }
    }
    WARN("100,000 entities: join " << build_ms << " ms for two frames; a frame (transforms and sync) with 1% moving "
                                   << ms[0] << " ms, 10% " << ms[1] << " ms, 100% " << ms[2] << " ms");
    CHECK(rig.Spatial().Stats().items_moved > 0);
}

TEST_CASE("Spatial sync: a static entity that moved and then loses the tag is found at its new place",
          "[spatial][sync]")
{
    SyRig rig;
    const ecs::Entity e = rig.Spawn(Vec3(1, 0, 0), true);
    for (u32 i = 0; i < 3; ++i)
    {
        rig.Frame();
    }
    rig.world.Get<ecs::LocalTransform>(e)->position = Vec3(40, 0, 0);  // refused by the static item
    for (u32 i = 0; i < 2; ++i)
    {
        rig.Frame();
    }
    CHECK(rig.Spatial().Set(kRenderSet)->Stats().static_moves_refused >= 1);
    REQUIRE(rig.world.Remove<SpatialStatic>(e) == ecs::Status::Ok);
    for (u32 i = 0; i < 3; ++i)
    {
        rig.Frame();
    }
    Vector<ecs::Entity> live;
    live.Push(e);
    CHECK(rig.Matches(live));  // fails without the move after MakeDynamic
}

TEST_CASE("Spatial sync: a member whose transform turns non-finite keeps its item and is counted", "[spatial][sync]")
{
    SyRig rig;
    const ecs::Entity e = rig.Spawn(Vec3(1, 0, 0));
    for (u32 i = 0; i < 3; ++i)
    {
        rig.Frame();
    }
    const u64 before = rig.Spatial().Stats().refused_bounds;
    rig.world.Get<ecs::LocalTransform>(e)->position = Vec3(Limits<f32>::QuietNaN(), 0, 0);
    for (u32 i = 0; i < 2; ++i)
    {
        rig.Frame();
    }
    CHECK(rig.Spatial().Stats().refused_bounds > before);
    CHECK(rig.Items(kRenderSet).Size() == 1);
    CHECK(rig.Items(kRenderSet)[0].box.min.x == 0.5f);
}

TEST_CASE(
    "Spatial sync: a membership edit that coincides with a box the sync refuses still removes the sets the entity no longer asks for",
    "[spatial][sync]")
{
    SyRig rig;
    const ecs::Entity e = rig.Spawn(Vec3(1, 0, 0), false, 3);
    for (u32 i = 0; i < 3; ++i)
    {
        rig.Frame();
    }
    REQUIRE(rig.Items(kPhysicsSet).Size() == 1);
    rig.world.Get<ecs::LocalTransform>(e)->position = Vec3(Limits<f32>::QuietNaN(), 0, 0);
    rig.world.Get<SpatialMembership>(e)->sets = 1;  // render only
    for (u32 i = 0; i < 3; ++i)
    {
        rig.Frame();
    }
    CHECK(rig.Items(kPhysicsSet).Size() == 0);
    CHECK(rig.Items(kRenderSet).Size() == 1);
}
