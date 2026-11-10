// The ECS side of the spatial module (Phase 18 Day 244): entities with the spatial components enter exactly their sets, and leave them when a component is
// removed, the entity is destroyed or the membership changes; no stale proxy and no leaked item remain (a model test of 2,000 random structural changes
// against a brute-force list); the SpatialWorld resource creates, finds and clears sets; the static tag routes to the static structure.
#include <catch2/catch_test_macros.hpp>

#include "algorithm.h"
#include "containers/vector.h"
#include "ecs/schedule.h"
#include "ecs/transform.h"
#include "ecs/world.h"
#include "lang/limits.h"
#include "lang/scalar.h"
#include "spatial/components.h"
#include "spatial/query.h"
#include "spatial/spatial_sync.h"
#include "spatial/spatial_world.h"
#include "spatial_test_rng.h"
#include "spatial_test_util.h"

using namespace engine_placeholder;
using namespace engine_placeholder::spatial;
using spatial_test::BoxAt;
using spatial_test::Rng;

namespace {

ecs::LocalTransform WcAt(f32 x, f32 y, f32 z) {
    ecs::LocalTransform t;
    t.position = Vec3(x, y, z);
    return t;
}

SpatialMembership WcIn(u32 sets, u32 layer = 0) {
    SpatialMembership m;
    m.sets = static_cast<u8>(sets);
    m.layer = static_cast<u8>(layer);
    return m;
}

constexpr u32 kBit(SetId id) {
    return 1u << id;
}

SpatialBounds WcUnit() {
    SpatialBounds b;
    b.local = BoxAt(Vec3(0, 0, 0), Vec3(0.5f, 0.5f, 0.5f));
    return b;
}

// A world with the spatial resource, the transform propagation and the sync, run for a frame at a time.
struct WcRig {
    ecs::World world;
    ecs::Schedule schedule;

    WcRig() {
        world.SetResource(ecs::TransformReport{});
        world.SetResource(SpatialWorld());
        schedule.Add(ecs::MakeTransformSystem("transforms"));
        schedule.Add(MakeSpatialSyncSystem("spatial_sync").InStage(ecs::kPostUpdate));
        REQUIRE(schedule.Build().ok);
    }
    void Frame() { schedule.Run(world, 0.016f); }
    SpatialWorld& Spatial() { return world.Resource<SpatialWorld>(); }
    ecs::Entity Spawn(f32 x, f32 y, f32 z, u32 sets, u32 layer = 0) { return world.Create(WcAt(x, y, z), ecs::WorldTransform{}, WcUnit(), WcIn(sets, layer)); }
    ecs::Entity SpawnStatic(f32 x, f32 y, f32 z, u32 sets) { return world.Create(WcAt(x, y, z), ecs::WorldTransform{}, WcUnit(), WcIn(sets), SpatialStatic{}); }
    // The sorted entity keys in a set.
    Vector<SpatialKey> Keys(SetId id) {
        Vector<SpatialKey> keys;
        OverlapKeys(*Spatial().Set(id), BoxAt(Vec3(0, 0, 0), Vec3(1e6f, 1e6f, 1e6f)), QueryFilter(), Order::Ordered, &keys);
        return keys;
    }
    u32 ItemCount(SetId id) {
        const SpatialSetStats s = Spatial().Set(id)->Stats();
        return s.static_items + s.dynamic_items;
    }
};

bool WcSame(const Vector<SpatialKey>& a, const Vector<SpatialKey>& b) {
    if (a.Size() != b.Size()) return false;
    for (usize i = 0; i < a.Size(); ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

} // namespace

TEST_CASE("SpatialWorld: it starts with the four well-known sets and creates, finds and clears more", "[spatial][world]") {
    SpatialWorld spatial;
    CHECK(spatial.SetCount() == 4);
    CHECK(spatial.Find("render") == kRenderSet);
    CHECK(spatial.Find("physics") == kPhysicsSet);
    CHECK(spatial.Find("audio") == kAudioSet);
    CHECK(spatial.Find("gameplay") == kGameplaySet);
    CHECK(spatial.Find("nothing") == kInvalidSetId);
    CHECK(spatial.NameOf(kAudioSet) == StringView("audio"));
    CHECK(spatial.Set(7) == nullptr);
    SpatialSetConfig grid;
    grid.dynamic = DynamicKind::Grid;
    const SetId crowd = spatial.CreateSet("crowd", grid);
    CHECK(crowd == 4);
    CHECK(spatial.Find("crowd") == crowd);
    spatial.Set(crowd)->AddDynamic(BoxAt(Vec3(0, 0, 0), Vec3(1, 1, 1)), 7, 0);
    CHECK(spatial.Set(crowd)->Stats().dynamic_items == 1);
    spatial.CreateSet("a");
    spatial.CreateSet("b");
    spatial.CreateSet("c");
    CHECK(spatial.SetCount() == kMaxSpatialSets);
    CHECK(spatial.CreateSet("one too many") == kInvalidSetId);
    CHECK(spatial.Stats().refused_sets == 1);
    CHECK(spatial.SetCount() == kMaxSpatialSets);
    spatial.Clear();
    CHECK(spatial.Set(crowd)->Stats().dynamic_items == 0);
    CHECK(spatial.Find("crowd") == crowd);
    const SetId name_cut = spatial.Find("a");
    CHECK(name_cut == 5);
}

TEST_CASE("Spatial sync: entities enter exactly the sets their membership names, with a proxy and a world box", "[spatial][world]") {
    WcRig rig;
    const ecs::Entity both = rig.Spawn(10, 0, 0, kBit(kRenderSet) | kBit(kPhysicsSet));
    const ecs::Entity render_only = rig.Spawn(20, 0, 0, kBit(kRenderSet));
    const ecs::Entity nowhere = rig.Spawn(30, 0, 0, 0);
    const ecs::Entity no_membership = rig.world.Create(WcAt(40, 0, 0), ecs::WorldTransform{}, WcUnit());
    rig.Frame();
    rig.Frame();
    Vector<SpatialKey> render;
    render.Push(both.ToU64());
    render.Push(render_only.ToU64());
    spatial_test::SortKeys(&render);
    Vector<SpatialKey> physics;
    physics.Push(both.ToU64());
    CHECK(WcSame(rig.Keys(kRenderSet), render));
    CHECK(WcSame(rig.Keys(kPhysicsSet), physics));
    CHECK(rig.Keys(kAudioSet).Size() == 0);
    REQUIRE(rig.world.Get<SpatialProxy>(both) != nullptr);
    REQUIRE(rig.world.Get<SpatialProxy>(nowhere) != nullptr);
    CHECK(rig.world.Get<SpatialProxy>(no_membership) == nullptr);
    const WorldBounds* box = rig.world.Get<WorldBounds>(both);
    REQUIRE(box != nullptr);
    CHECK(box->box.min.x == 9.5f);
    CHECK(box->box.max.x == 10.5f);
    CHECK(rig.Spatial().Stats().joined == 3);
    CHECK(rig.Spatial().Stats().items_added == 3);
}

TEST_CASE("Spatial sync: the static tag routes the item to the static structure and toggling it moves the item", "[spatial][world]") {
    WcRig rig;
    const ecs::Entity wall = rig.SpawnStatic(0, 0, 0, kBit(kPhysicsSet));
    const ecs::Entity crate = rig.Spawn(5, 0, 0, kBit(kPhysicsSet));
    rig.Frame();
    rig.Frame();
    CHECK(rig.Spatial().Set(kPhysicsSet)->Stats().static_items == 1);
    CHECK(rig.Spatial().Set(kPhysicsSet)->Stats().dynamic_items == 1);
    CHECK(rig.world.Get<SpatialProxy>(wall)->is_static);
    CHECK_FALSE(rig.world.Get<SpatialProxy>(crate)->is_static);
    REQUIRE(rig.world.Add<SpatialStatic>(crate, SpatialStatic{}) == ecs::Status::Ok);
    rig.Frame();
    rig.Frame();
    CHECK(rig.Spatial().Set(kPhysicsSet)->Stats().static_items == 2);
    CHECK(rig.Spatial().Set(kPhysicsSet)->Stats().dynamic_items == 0);
    REQUIRE(rig.world.Remove<SpatialStatic>(wall) == ecs::Status::Ok);
    rig.Frame();
    rig.Frame();
    CHECK(rig.Spatial().Set(kPhysicsSet)->Stats().static_items == 1);
    CHECK(rig.Spatial().Set(kPhysicsSet)->Stats().dynamic_items == 1);
    CHECK(rig.Keys(kPhysicsSet).Size() == 2);
}

TEST_CASE("Spatial sync: removing a component, destroying the entity or changing the membership updates the sets", "[spatial][world]") {
    WcRig rig;
    const ecs::Entity a = rig.Spawn(1, 0, 0, kBit(kRenderSet) | kBit(kAudioSet));
    const ecs::Entity b = rig.Spawn(2, 0, 0, kBit(kRenderSet));
    const ecs::Entity c = rig.Spawn(3, 0, 0, kBit(kRenderSet));
    const ecs::Entity d = rig.Spawn(4, 0, 0, kBit(kRenderSet));
    rig.Frame();
    rig.Frame();
    CHECK(rig.ItemCount(kRenderSet) == 4);
    REQUIRE(rig.world.Remove<SpatialBounds>(a) == ecs::Status::Ok);
    REQUIRE(rig.world.Remove<SpatialMembership>(b) == ecs::Status::Ok);
    REQUIRE(rig.world.Destroy(c) == ecs::Status::Ok);
    rig.world.Get<SpatialMembership>(d)->sets = static_cast<u8>(kBit(kPhysicsSet));
    rig.world.Stamp<SpatialMembership>(d);
    rig.Frame();
    rig.Frame();
    rig.Frame();
    CHECK(rig.ItemCount(kRenderSet) == 0);
    CHECK(rig.ItemCount(kAudioSet) == 0);
    CHECK(rig.ItemCount(kPhysicsSet) == 1);
    CHECK(rig.world.Get<SpatialProxy>(a) == nullptr);
    CHECK(rig.world.Get<WorldBounds>(a) == nullptr);
    CHECK(rig.world.Get<SpatialProxy>(b) == nullptr);
    REQUIRE(rig.world.Get<SpatialProxy>(d) != nullptr);
    CHECK(rig.Spatial().Stats().left == 3);
    // Put the component back: the entity joins again.
    REQUIRE(rig.world.Add<SpatialBounds>(a, WcUnit()) == ecs::Status::Ok);
    rig.Frame();
    rig.Frame();
    CHECK(rig.ItemCount(kRenderSet) == 1);
    CHECK(rig.ItemCount(kAudioSet) == 1);
    // A layer change re-adds the item with the new layer.
    rig.world.Get<SpatialMembership>(a)->layer = 3;
    rig.world.Stamp<SpatialMembership>(a);
    rig.Frame();
    rig.Frame();
    QueryFilter on_three;
    on_three.mask = 1u << 3;
    Vector<SpatialKey> keys;
    OverlapKeys(*rig.Spatial().Set(kRenderSet), BoxAt(Vec3(0, 0, 0), Vec3(1e6f, 1e6f, 1e6f)), on_three, Order::Ordered, &keys);
    REQUIRE(keys.Size() == 1);
    CHECK(keys[0] == a.ToU64());
    CHECK(rig.ItemCount(kRenderSet) == 1);
}

TEST_CASE("Spatial sync: an entity destroyed in the frame it joined leaves no item behind", "[spatial][world]") {
    WcRig rig;
    const ecs::Entity e = rig.Spawn(1, 0, 0, kBit(kRenderSet));
    rig.Frame(); // the item is made; the proxy lands at the sync point
    REQUIRE(rig.world.Destroy(e) == ecs::Status::Ok);
    rig.Frame();
    rig.Frame();
    CHECK(rig.ItemCount(kRenderSet) == 0);
    // An entity destroyed before the sync ever saw it.
    const ecs::Entity f = rig.Spawn(2, 0, 0, kBit(kRenderSet));
    REQUIRE(rig.world.Destroy(f) == ecs::Status::Ok);
    rig.Frame();
    rig.Frame();
    CHECK(rig.ItemCount(kRenderSet) == 0);
}

TEST_CASE("Spatial sync: a non-finite or empty box is refused and counted, bits of sets that do not exist are counted", "[spatial][world]") {
    WcRig rig;
    SpatialBounds nan_box = WcUnit();
    nan_box.local.max.x = Limits<f32>::QuietNaN();
    rig.world.Create(WcAt(0, 0, 0), ecs::WorldTransform{}, nan_box, WcIn(kBit(kRenderSet)));
    SpatialBounds empty_box;
    empty_box.local = Aabb::Empty();
    rig.world.Create(WcAt(0, 0, 0), ecs::WorldTransform{}, empty_box, WcIn(kBit(kRenderSet)));
    rig.world.Create(WcAt(Limits<f32>::QuietNaN(), 0, 0), ecs::WorldTransform{}, WcUnit(), WcIn(kBit(kRenderSet)));
    rig.Frame();
    CHECK(rig.ItemCount(kRenderSet) == 0);
    CHECK(rig.Spatial().Stats().refused_bounds == 3);
    rig.Frame();
    CHECK(rig.Spatial().Stats().refused_bounds == 6); // retried every frame
    const ecs::Entity odd = rig.Spawn(0, 0, 0, kBit(kRenderSet) | kBit(6));
    rig.Frame();
    CHECK(rig.ItemCount(kRenderSet) == 1);
    CHECK(rig.Spatial().Stats().unknown_set_bits >= 1);
    CHECK(rig.world.Get<SpatialProxy>(odd) != nullptr);
}

TEST_CASE("Spatial sync: 2,000 random structural changes keep the sets equal to a brute-force list", "[spatial][world]") {
    WcRig rig;
    rig.Spatial().CreateSet("extra");
    struct Entry {
        ecs::Entity entity;
        bool bounds = true;
        bool membership = true;
        bool transform = true;
        u32 sets = 0;
        u32 layer = 0;
        bool is_static = false;
    };
    Vector<Entry> model;
    Rng rng(0x5EEDull);
    for (u32 frame = 0; frame < 2000; ++frame) {
        const u32 op = rng.Below(10);
        if (op < 3 || model.Size() < 5) {
            Entry e;
            e.sets = rng.Below(32) & 0x1Fu;
            e.layer = rng.Below(4);
            e.is_static = rng.Below(3) == 0;
            e.entity = e.is_static ? rig.SpawnStatic(rng.Float(-50, 50), rng.Float(-50, 50), rng.Float(-50, 50), e.sets)
                                   : rig.Spawn(rng.Float(-50, 50), rng.Float(-50, 50), rng.Float(-50, 50), e.sets, e.layer);
            if (e.is_static) {
                rig.world.Get<SpatialMembership>(e.entity)->layer = static_cast<u8>(e.layer);
            }
            model.Push(e);
        } else {
            Entry& e = model[rng.Below(static_cast<u32>(model.Size()))];
            switch (op) {
            case 3: // destroy
                REQUIRE(rig.world.Destroy(e.entity) == ecs::Status::Ok);
                e = model[model.Size() - 1];
                model.Pop();
                break;
            case 4: // remove or restore the bounds
                if (e.bounds) {
                    REQUIRE(rig.world.Remove<SpatialBounds>(e.entity) == ecs::Status::Ok);
                } else {
                    REQUIRE(rig.world.Add<SpatialBounds>(e.entity, WcUnit()) == ecs::Status::Ok);
                }
                e.bounds = !e.bounds;
                break;
            case 5: // remove or restore the membership
                if (e.membership) {
                    REQUIRE(rig.world.Remove<SpatialMembership>(e.entity) == ecs::Status::Ok);
                } else {
                    REQUIRE(rig.world.Add<SpatialMembership>(e.entity, WcIn(e.sets, e.layer)) == ecs::Status::Ok);
                }
                e.membership = !e.membership;
                break;
            case 6: // change the sets
                if (e.membership) {
                    e.sets = rng.Below(32) & 0x1Fu;
                    rig.world.Get<SpatialMembership>(e.entity)->sets = static_cast<u8>(e.sets);
                    rig.world.Stamp<SpatialMembership>(e.entity);
                }
                break;
            case 7: // change the layer
                if (e.membership) {
                    e.layer = rng.Below(4);
                    rig.world.Get<SpatialMembership>(e.entity)->layer = static_cast<u8>(e.layer);
                    rig.world.Stamp<SpatialMembership>(e.entity);
                }
                break;
            case 8: // toggle the static tag
                if (e.is_static) {
                    REQUIRE(rig.world.Remove<SpatialStatic>(e.entity) == ecs::Status::Ok);
                } else {
                    REQUIRE(rig.world.Add<SpatialStatic>(e.entity, SpatialStatic{}) == ecs::Status::Ok);
                }
                e.is_static = !e.is_static;
                break;
            default: // remove or restore the transform
                if (e.transform) {
                    REQUIRE(rig.world.Remove<ecs::WorldTransform>(e.entity) == ecs::Status::Ok);
                } else {
                    REQUIRE(rig.world.Add<ecs::WorldTransform>(e.entity, ecs::WorldTransform{}) == ecs::Status::Ok);
                }
                e.transform = !e.transform;
                break;
            }
        }
        rig.Frame();
        if (frame % 7 == 0 || frame == 1999) {
            rig.Frame(); // the proxies of the last frame have landed
            for (SetId id = 0; id < rig.Spatial().SetCount(); ++id) {
                Vector<SpatialKey> expect;
                u32 static_count = 0;
                for (usize i = 0; i < model.Size(); ++i) {
                    const Entry& e = model[i];
                    if (e.bounds && e.membership && e.transform && ((e.sets >> id) & 1u) != 0) {
                        expect.Push(e.entity.ToU64());
                        static_count += e.is_static ? 1u : 0u;
                    }
                }
                spatial_test::SortKeys(&expect);
                REQUIRE(WcSame(rig.Keys(id), expect));
                REQUIRE(rig.ItemCount(id) == expect.Size());
                REQUIRE(rig.Spatial().Set(id)->Stats().static_items == static_count);
            }
        }
    }
}

TEST_CASE("Spatial sync: an entity destroyed by a command applied before the proxy leaves no item behind", "[spatial][world]") {
    ecs::World world;
    ecs::Schedule schedule;
    world.SetResource(ecs::TransformReport{});
    world.SetResource(SpatialWorld());
    ecs::Entity victim;
    bool reap = false;
    schedule.Add(ecs::MakeTransformSystem("transforms"));
    // Runs before the sync in the same stage: its destroy command is applied before the sync's command that adds the proxy, which is then skipped.
    schedule.Add(ecs::SystemDesc("reaper", ecs::detail::SystemFn([&](ecs::SystemContext& context) {
                                     if (reap) {
                                         context.Commands().Destroy(victim);
                                     }
                                 }))
                     .InStage(ecs::kPostUpdate));
    schedule.Add(MakeSpatialSyncSystem("spatial_sync").InStage(ecs::kPostUpdate));
    REQUIRE(schedule.Build().ok);
    const ecs::Entity keeper = world.Create(WcAt(2, 0, 0), ecs::WorldTransform{}, WcUnit(), WcIn(kBit(kRenderSet)));
    (void)keeper;
    victim = world.Create(WcAt(1, 0, 0), ecs::WorldTransform{}, WcUnit(), WcIn(kBit(kRenderSet)));
    reap = true; // in the frame the sync sees the victim, the reaper's command destroys it first
    schedule.Run(world, 0.016f);
    reap = false;
    schedule.Run(world, 0.016f);
    schedule.Run(world, 0.016f);
    const SpatialSetStats stats = world.Resource<SpatialWorld>().Set(kRenderSet)->Stats();
    CHECK(stats.static_items + stats.dynamic_items == 1);
}

TEST_CASE("Spatial sync: a box that turns bad after the join is refused and counted, the item stays", "[spatial][world]") {
    WcRig rig;
    const ecs::Entity e = rig.Spawn(1, 0, 0, kBit(kRenderSet));
    rig.Frame();
    rig.Frame();
    REQUIRE(rig.ItemCount(kRenderSet) == 1);
    rig.world.Get<ecs::LocalTransform>(e)->position = Vec3(Limits<f32>::QuietNaN(), 0, 0);
    rig.world.Get<SpatialMembership>(e)->sets = static_cast<u8>(kBit(kRenderSet) | kBit(kAudioSet));
    rig.world.Stamp<SpatialMembership>(e);
    rig.Frame(); // would abort in SpatialSet::AddDynamic without the check
    rig.Frame();
    CHECK(rig.Spatial().Stats().refused_bounds >= 1);
    CHECK(rig.ItemCount(kRenderSet) == 1);
    CHECK(rig.ItemCount(kAudioSet) == 0);
}

TEST_CASE("Spatial sync: a layer of 32 or more is refused, unknown set bits are counted once, and the item counters balance", "[spatial][world]") {
    WcRig rig;
    rig.Spawn(0, 0, 0, kBit(kRenderSet), 40);
    rig.Frame();
    CHECK(rig.ItemCount(kRenderSet) == 0);
    CHECK(rig.Spatial().Stats().refused_bounds == 1);
    rig.Spawn(1, 0, 0, kBit(kRenderSet) | kBit(6));
    for (u32 i = 0; i < 4; ++i) rig.Frame();
    CHECK(rig.Spatial().Stats().unknown_set_bits == 1);
    const ecs::Entity gone = rig.Spawn(2, 0, 0, kBit(kRenderSet) | kBit(kPhysicsSet));
    rig.Frame();
    rig.Frame();
    REQUIRE(rig.world.Destroy(gone) == ecs::Status::Ok);
    for (u32 i = 0; i < 4; ++i) rig.Frame();
    const SpatialWorldStats& stats = rig.Spatial().Stats();
    CHECK(stats.items_added == 3);
    CHECK(stats.items_removed == 2);
    CHECK(stats.left == 1);
}
