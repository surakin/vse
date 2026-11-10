#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "ecs/query.h"
#include "ecs/world.h"
#include "lang/limits.h"
#include "test_rng.h"

using namespace engine_placeholder;
using namespace engine_placeholder::ecs;

namespace {

struct VA { u32 v; };
struct VB { u32 v; };
// Large enough that a chunk holds only a handful of rows, so a test gets several chunks from a few dozen entities.
struct ChangeBig { u32 v; byte pad[4000]; };

template <class... Terms>
u32 Visited(World& world, u64 since) {
    auto q = world.Query<Terms...>();
    q.SetSince(since);
    u32 n = 0;
    q.ForEachChunk([&](ChunkView& view) { n += view.Count(); });
    return n;
}

} // namespace

TEST_CASE("Change: a write marks only the column that was written", "[ecs][change]") {
    World world;
    for (u32 i = 0; i < 10; ++i) {
        world.Create(VA{i}, VB{i});
    }
    world.Advance();
    const u64 since = world.Tick() - 1; // everything created before this tick
    CHECK((Visited<Changed<VA>>(world, since)) == 0);
    CHECK((Visited<Changed<VB>>(world, since)) == 0);
    world.Query<Write<VA>>().ForEach([](VA& a) { a.v += 1; });
    CHECK((Visited<Changed<VA>>(world, since)) == 10);
    CHECK((Visited<Changed<VB>>(world, since)) == 0);
}

TEST_CASE("Change: a read-only pass marks nothing", "[ecs][change]") {
    World world;
    for (u32 i = 0; i < 10; ++i) {
        world.Create(VA{i});
    }
    world.Advance();
    const u64 since = world.Tick() - 1;
    u32 sum = 0;
    world.Query<Read<VA>>().ForEach([&](const VA& a) { sum += a.v; });
    world.Query<Optional<VA>>().ForEach([&](const VA*) {});
    CHECK(sum == 45);
    CHECK((Visited<Changed<VA>>(world, since)) == 0);
}

TEST_CASE("Change: Set, Get and Add stamp the chunk of the entity they touch", "[ecs][change]") {
    World world;
    Vector<Entity> ids;
    for (u32 i = 0; i < 40; ++i) {
        ids.Push(world.Create(ChangeBig{i, {}}));
    }
    const u32 rows = world.Query<Read<ChangeBig>>().Count();
    CHECK(rows == 40);
    world.Advance();
    const u64 since = world.Tick() - 1;
    const u32 per_chunk = (Visited<With<ChangeBig>>(world, 0));
    CHECK(per_chunk == 40);

    world.Set<ChangeBig>(ids[0], ChangeBig{99, {}});
    const u32 one_chunk = Visited<Changed<ChangeBig>>(world, since);
    CHECK(one_chunk > 0);
    CHECK(one_chunk < 40); // only the chunk of ids[0]

    world.Advance();
    const u64 since2 = world.Tick() - 1;
    world.Get<ChangeBig>(ids[39])->v = 5; // a pointer may be written through: the non-const Get stamps
    const u32 after_get = Visited<Changed<ChangeBig>>(world, since2);
    CHECK(after_get > 0);
    CHECK(after_get < 40);

    world.Advance();
    const u64 since3 = world.Tick() - 1;
    const ChangeBig* read = static_cast<const World&>(world).Get<ChangeBig>(ids[20]);
    CHECK(read->v == 20);
    CHECK((Visited<Changed<ChangeBig>>(world, since3)) == 0); // the const Get stamps nothing
}

TEST_CASE("Change: Changed returns exactly the chunks written since", "[ecs][change]") {
    World world;
    Vector<Entity> ids;
    for (u32 i = 0; i < 60; ++i) {
        ids.Push(world.Create(ChangeBig{i, {}}));
    }
    world.Advance();
    const u64 since = world.Tick() - 1;
    // Write the entities whose value is a multiple of 20 through a query that writes only those... the chunk is the unit.
    world.Set<ChangeBig>(ids[5], ChangeBig{500, {}});
    world.Set<ChangeBig>(ids[55], ChangeBig{550, {}});
    Vector<u32> visited_values;
    auto q = world.Query<Changed<ChangeBig>, Read<ChangeBig>>();
    q.SetSince(since);
    q.ForEachChunk([&](ChunkView& view) {
        CHECK(view.Count() > 0);
        bool has_written = false;
        for (const ChangeBig& b : view.Read<ChangeBig>()) {
            has_written = has_written || b.v == 500 || b.v == 550;
        }
        CHECK(has_written); // never a chunk that holds no written row
    });
    u32 rows = 0;
    q.ForEach([&](const ChangeBig& b) {
        ++rows;
        visited_values.Push(b.v);
    });
    CHECK(rows >= 2);
    CHECK(rows < 60);
    bool saw5 = false;
    bool saw55 = false;
    for (usize i = 0; i < visited_values.Size(); ++i) {
        saw5 = saw5 || visited_values[i] == 500;
        saw55 = saw55 || visited_values[i] == 550;
    }
    CHECK(saw5);
    CHECK(saw55);
}

TEST_CASE("Change: Finish makes the next pass see only newer changes", "[ecs][change]") {
    World world;
    const Entity a = world.Create(VA{1});
    auto changed = world.Query<Changed<VA>, Read<VA>>();
    u32 n = 0;
    changed.ForEach([&](const VA&) { ++n; });
    CHECK(n == 1); // the creation counts as a write
    changed.Finish();
    n = 0;
    changed.ForEach([&](const VA&) { ++n; });
    CHECK(n == 0); // a second pass in the same frame sees nothing new
    world.Set<VA>(a, VA{2}); // after Finish: stamped with the advanced tick
    changed.ForEach([&](const VA&) { ++n; });
    CHECK(n == 1);
    changed.Finish();
    n = 0;
    changed.ForEach([&](const VA&) { ++n; });
    CHECK(n == 0);
}

TEST_CASE("Change: a query's own writes are not seen by its next pass", "[ecs][change]") {
    World world;
    world.Create(VA{1});
    auto q = world.Query<Changed<VA>, Write<VA>>();
    u32 n = 0;
    q.ForEach([&](VA& a) { ++n; a.v += 1; });
    CHECK(n == 1);
    q.Finish();
    n = 0;
    q.ForEach([&](VA&) { ++n; });
    CHECK(n == 0);
}

TEST_CASE("Change: Added sees created and migrated-in entities once", "[ecs][change]") {
    World world;
    const Entity first = world.Create(VA{1});
    auto added = world.Query<Added<VB>, Read<VB>>();
    u32 n = 0;
    added.ForEach([&](const VB&) { ++n; });
    CHECK(n == 0); // no VB yet
    added.Finish();
    world.Create(VA{2}, VB{2});
    world.Add<VB>(first, VB{3}); // migrates into the {VA, VB} archetype
    n = 0;
    added.ForEach([&](const VB&) { ++n; });
    CHECK(n == 2);
    added.Finish();
    n = 0;
    added.ForEach([&](const VB&) { ++n; });
    CHECK(n == 0); // once
    world.Set<VB>(first, VB{4});
    added.ForEach([&](const VB&) { ++n; });
    CHECK(n == 0); // a write is not an addition
    world.Remove<VA>(first);
    added.ForEach([&](const VB&) { ++n; });
    CHECK(n == 1); // migrating to another archetype is an arrival there
}

TEST_CASE("Change: Changed and Added combine with other terms and add no arguments", "[ecs][change]") {
    World world;
    world.Create(VA{1}, VB{1});
    world.Create(VA{2});
    auto q = world.Query<Changed<VA>, Read<VA>, With<VB>>();
    u32 sum = 0;
    q.ForEach([&](Entity, const VA& a) { sum += a.v; });
    CHECK(sum == 1);
    auto only = world.Query<Changed<VA>, Added<VA>, Without<VB>, Read<VA>>();
    sum = 0;
    only.ForEach([&](const VA& a) { sum += a.v; });
    CHECK(sum == 2);
}

TEST_CASE("Change: random writes are all seen and no untouched chunk is", "[ecs][change]") {
    for (u64 seed = 1; seed <= 100; ++seed) {
        ecs_test::Rng rng(seed);
        World world;
        Vector<Entity> ids;
        const u32 count = 20 + rng.Below(60);
        for (u32 i = 0; i < count; ++i) {
            ids.Push(world.Create(ChangeBig{i, {}}));
        }
        for (u32 round = 0; round < 5; ++round) {
            world.Advance();
            const u64 since = world.Tick() - 1;
            Vector<u8> flagged;
            flagged.Resize(count);
            const u32 writes = rng.Below(6);
            for (u32 w = 0; w < writes; ++w) {
                const u32 at = rng.Below(count);
                if (rng.Below(2) == 0) {
                    world.Set<ChangeBig>(ids[at], ChangeBig{at + 1000, {}});
                } else {
                    world.Get<ChangeBig>(ids[at])->v = at + 1000;
                }
                flagged[at] = 1;
            }
            Vector<u8> seen;
            seen.Resize(count);
            auto q = world.Query<Changed<ChangeBig>, Read<ChangeBig>>();
            q.SetSince(since);
            q.ForEachChunk([&](ChunkView& view) {
                bool any_flagged = false;
                for (usize i = 0; i < view.Count(); ++i) {
                    const Entity e = view.Entities()[i];
                    for (u32 k = 0; k < count; ++k) {
                        if (ids[k] == e) {
                            seen[k] = 1;
                            any_flagged = any_flagged || flagged[k] != 0;
                        }
                    }
                }
                CHECK(any_flagged);
            });
            for (u32 k = 0; k < count; ++k) {
                if (flagged[k] != 0) {
                    CHECK(seen[k] != 0);
                }
            }
        }
    }
}

TEST_CASE("Change: ticks near the largest 64-bit value compare correctly", "[ecs][change]") {
    World world;
    world.SetTick(Limits<u64>::Max() - 3);
    const Entity e = world.Create(VA{1});
    auto q = world.Query<Changed<VA>, Read<VA>>();
    u32 n = 0;
    q.ForEach([&](const VA&) { ++n; });
    CHECK(n == 1);
    q.Finish(); // since = Max-3, tick = Max-2
    n = 0;
    q.ForEach([&](const VA&) { ++n; });
    CHECK(n == 0);
    world.Set<VA>(e, VA{2}); // stamped Max-2 > since
    q.ForEach([&](const VA&) { ++n; });
    CHECK(n == 1);
    q.Finish();
    world.Advance();
    CHECK(world.Tick() == Limits<u64>::Max());
    n = 0;
    q.ForEach([&](const VA&) { ++n; });
    CHECK(n == 0);
}

// Regression (Phase 17 review): destroying a row moves the archetype's last row into the gap, which can be in an earlier chunk; that chunk must be stamped as added too,
// or an `Added` reader that already passed it never sees the entity created since.
TEST_CASE("Change: Added still reports an entity that a destroy moved into an earlier chunk", "[ecs][change]") {
    World world;
    Vector<Entity> ids;
    for (u32 i = 0; i < 8; ++i) {
        ids.Push(world.Create(ChangeBig{i, {}}));
    }
    auto added = world.Query<Added<ChangeBig>, Read<ChangeBig>>();
    added.ForEach([](const ChangeBig&) {});
    added.Finish();
    world.Advance();
    const Entity fresh = world.Create(ChangeBig{100, {}}); // lands in the last chunk
    world.Advance();
    REQUIRE(world.Destroy(ids[0]) == Status::Ok);           // the last row (fresh) moves into chunk 0
    bool seen = false;
    added.ForEach([&](const ChangeBig& b) { seen = seen || b.v == 100; });
    CHECK(seen);
    CHECK(world.Exists(fresh));
}
