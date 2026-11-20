#include <catch2/catch_test_macros.hpp>

#include "ecs/schedule.h"
#include "ecs/world.h"

using namespace vse;
using namespace vse::ecs;

namespace
{
struct Tracked
{
    static constexpr bool kTrackRemoval = true;
    u32 value;
};
struct Untracked
{
    u32 value;
};
}  // namespace

TEST_CASE("Removal log: destroying an entity logs its tracked component's value", "[ecs][removal]")
{
    World world;
    const Entity a = world.Create(Tracked{7}, Untracked{1});
    const Entity b = world.Create(Tracked{9});
    world.Destroy(a);
    REQUIRE(world.RemovedCount<Tracked>() == 1);
    CHECK(world.RemovedAt<Tracked>(0).entity == a);
    CHECK(world.RemovedAt<Tracked>(0).value.value == 7);
    CHECK(world.RemovedCount<Untracked>() == 0);
    world.Destroy(b);
    REQUIRE(world.RemovedCount<Tracked>() == 2);
    CHECK(world.RemovedAt<Tracked>(1).value.value == 9);
    CHECK(world.RemovedAt<Tracked>(1).seq > world.RemovedAt<Tracked>(0).seq);
}

TEST_CASE("Removal log: removing the component logs it, replacing it does not", "[ecs][removal]")
{
    World world;
    const Entity e = world.Create(Tracked{3}, Untracked{1});
    world.Set<Tracked>(e, Tracked{4});
    CHECK(world.RemovedCount<Tracked>() == 0);
    CHECK(world.Remove<Tracked>(e) == Status::Ok);
    REQUIRE(world.RemovedCount<Tracked>() == 1);
    CHECK(world.RemovedAt<Tracked>(0).value.value == 4);
    CHECK(world.Exists(e));
    CHECK(world.Get<Untracked>(e)->value == 1);
}

TEST_CASE("Removal log: entries live for the current and the next frame only", "[ecs][removal]")
{
    World world;
    const Entity a = world.Create(Tracked{1});
    world.Destroy(a);
    world.EndFrame();
    CHECK(world.RemovedCount<Tracked>() == 1);  // the next frame still sees it
    const Entity b = world.Create(Tracked{2});
    world.Destroy(b);
    CHECK(world.RemovedCount<Tracked>() == 2);
    world.EndFrame();
    REQUIRE(world.RemovedCount<Tracked>() == 1);
    CHECK(world.RemovedAt<Tracked>(0).value.value == 2);  // a's entry went
    world.EndFrame();
    CHECK(world.RemovedCount<Tracked>() == 0);
}

TEST_CASE("Removal log: a component that does not track removal has an empty log", "[ecs][removal]")
{
    World world;
    const Entity e = world.Create(Untracked{1});
    world.Destroy(e);
    CHECK(world.RemovedCount<Untracked>() == 0);
}

TEST_CASE("Removal log: removals made by a command buffer are logged when it plays", "[ecs][removal]")
{
    World world;
    const Entity e = world.Create(Tracked{5});
    CommandBuffer commands;
    commands.Destroy(e);
    CHECK(world.RemovedCount<Tracked>() == 0);
    world.Apply(commands);
    CHECK(world.RemovedCount<Tracked>() == 1);
}
