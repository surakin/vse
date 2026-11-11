// The type-erased entity calls a loader uses (Phase 19 Day 260): CreateFromSet, AddDefault, RemoveById, CellOf, HasById, EnsureArchetype.

#include <catch2/catch_test_macros.hpp>

#include "ecs/query.h"
#include "ecs/world.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ecs;

namespace {

struct RawA { u32 v = 7; };
struct RawB { f32 x = 1.5f; };
struct RawTag {};
struct RawNoDefault {
    int v;
    explicit RawNoDefault(int x) : v(x) {}
};

} // namespace

TEST_CASE("Raw access: CreateFromSet default-constructs exactly the set", "[ecs][raw]") {
    World world;
    ComponentSet set;
    set.Add(ComponentOf<RawA>());
    set.Add(ComponentOf<RawB>());
    set.Add(ComponentOf<RawTag>());
    REQUIRE(world.EnsureArchetype(set));
    const Entity e = world.CreateFromSet(set);
    REQUIRE(e.IsValid());
    CHECK(world.Has<RawA>(e));
    CHECK(world.Has<RawB>(e));
    CHECK(world.Has<RawTag>(e));
    CHECK(world.Get<RawA>(e)->v == 7);
    CHECK(world.Get<RawB>(e)->x == 1.5f);
    CHECK(world.HasById(e, ComponentOf<RawTag>()));
    CHECK(world.CellOf(e, ComponentOf<RawTag>()) == nullptr); // a tag has no cell
    CHECK(world.CellOf(e, ComponentOf<RawA>()) == world.Get<RawA>(e));
    CHECK(world.Validate().Ok());
}

TEST_CASE("Raw access: a type without a default constructor is refused and nothing is made", "[ecs][raw]") {
    World world;
    ComponentSet set;
    set.Add(ComponentOf<RawA>());
    set.Add(ComponentOf<RawNoDefault>());
    CHECK_FALSE(world.CreateFromSet(set).IsValid());
    CHECK(world.Stats().entities == 0);
    const Entity e = world.Create(RawA{});
    CHECK(world.AddDefault(e, ComponentOf<RawNoDefault>()) == Status::Refused);
    CHECK_FALSE(world.Has<RawNoDefault>(e));
}

TEST_CASE("Raw access: AddDefault and RemoveById move an entity like Add and Remove", "[ecs][raw]") {
    World world;
    const Entity e = world.Create(RawA{42});
    CHECK(world.AddDefault(e, ComponentOf<RawB>()) == Status::Ok);
    CHECK(world.AddDefault(e, ComponentOf<RawB>()) == Status::AlreadyHas);
    CHECK(world.Get<RawB>(e)->x == 1.5f);
    CHECK(world.Get<RawA>(e)->v == 42); // the move kept the old component
    CHECK(world.RemoveById(e, ComponentOf<RawA>()) == Status::Ok);
    CHECK(world.RemoveById(e, ComponentOf<RawA>()) == Status::MissingComponent);
    CHECK_FALSE(world.Has<RawA>(e));
    CHECK(world.Has<RawB>(e));
    CHECK(world.AddDefault(e, kInvalidComponent) == Status::Refused);
    CHECK(world.RemoveById(Entity{}, ComponentOf<RawB>()) == Status::DeadEntity);
    CHECK(world.CellOf(Entity{}, ComponentOf<RawB>()) == nullptr);
    CHECK(world.Validate().Ok());
}

TEST_CASE("Raw access: the writable CellOf stamps the component as changed", "[ecs][raw]") {
    World world;
    const Entity e = world.Create(RawA{1});
    world.Advance();
    const u64 since = world.Tick() - 1;
    u32 seen = 0;
    {
        auto q = world.Query<Changed<RawA>>();
        q.SetSince(since);
        q.ForEachChunk([&](ChunkView& view) { seen += view.Count(); });
    }
    CHECK(seen == 0);
    static_cast<RawA*>(world.CellOf(e, ComponentOf<RawA>()))->v = 2;
    {
        auto q = world.Query<Changed<RawA>>();
        q.SetSince(since);
        q.ForEachChunk([&](ChunkView& view) { seen += view.Count(); });
    }
    CHECK(seen == 1);
    const World& view = world;
    CHECK(static_cast<const RawA*>(view.CellOf(e, ComponentOf<RawA>()))->v == 2);
}
