// The storage benchmark (Phase 19 Day 265): the measurements of the pass condition in section 4 of
// docs/superpowers/specs/2026-11-11-storage-design.md. Hidden (`[.]`): run it by name in a Release tree, `storage_tests
// "[storage-bench]"`. It prints one line per measurement and checks only that the answers are right.

#include <catch2/catch_test_macros.hpp>

#include "database/sqlite/sqlite_database.h"
#include "ecs/transform.h"
#include "jobs/scheduler.h"
#include "os/console.h"
#include "os/os.h"
#include "spatial/components.h"
#include "storage/async_storage.h"
#include "storage/builtin_components.h"
#include "storage/memory_store.h"
#include "storage/save_session.h"
#include "storage/sql_store.h"
#include "storage/world_storage.h"
#include "temp_dir.h"

namespace engine_placeholder::storage
{
namespace
{

struct BnActor
{
    i32 hp = 0;
    u16 level = 0;
    f32 speed = 0;
    f64 score = 0;
};
struct BnFlag
{
    i32 value = 0;
};

Status RegisterBench(ComponentRegistry* r)
{
    ENGINE_TRY(RegisterBuiltinComponents(r));
    ENGINE_TRY(r->Register<BnActor>({.id = 5000,
                                     .name = "bn_actor",
                                     .flags = kComponentStored | kComponentQueryable,
                                     .fields = {Field<&BnActor::hp>("hp"), Field<&BnActor::level>("level"),
                                                Field<&BnActor::speed>("speed"), Field<&BnActor::score>("score")}}));
    ENGINE_TRY(r->Register<BnFlag>({.id = 5001, .name = "bn_flag", .fields = {Field<&BnFlag::value>("value")}}));
    return Status::Success();
}

struct BnRng
{
    u64 s;
    u32 Next()
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<u32>(s >> 33);
    }
};

// N entities with a key, a transform, bounds, membership, an actor and a flag (six components with the key), in
// hierarchies of depth up to 5.
void BenchPopulate(ecs::World& world, KeyIndex& index, Vector<ecs::Entity>* made, usize count)
{
    BnRng rng{77};
    KeyGenerator keys = KeyGenerator::Seeded(77);
    Vector<u8> depth;
    for (usize i = 0; i < count; ++i)
    {
        ecs::LocalTransform local;
        local.position = Vec3(static_cast<f32>(rng.Next() % 1000), 0, static_cast<f32>(rng.Next() % 1000));
        spatial::SpatialBounds bounds;
        bounds.local = Aabb::FromMinMax(Vec3(-1, -1, -1), Vec3(1, 1, 1));
        spatial::SpatialMembership membership;
        membership.sets = 3;
        const ecs::Entity e = world.Create(EntityKeyComponent{keys.Next()}, local, bounds, membership,
                                           BnActor{static_cast<i32>(rng.Next() % 200), 1, 2.0f, 3.0}, BnFlag{1});
        u8 d = 0;
        if (i > 0 && rng.Next() % 3 == 0)
        {
            const usize p = rng.Next() % made->Size();
            if (depth[p] < 5)
            {
                world.Set(e, ecs::Parent{(*made)[p]});
                world.Set(e, ecs::HierarchyDepth{static_cast<u32>(depth[p]) + 1});
                d = depth[p] + 1;
            }
        }
        made->Push(e);
        depth.Push(d);
    }
    REQUIRE(index.Sync(world).Ok());
}

u64 Ms(u64 from)
{
    return (os::MonotonicTimeNanos() - from) / 1000000;
}
u64 Us(u64 from)
{
    return (os::MonotonicTimeNanos() - from) / 1000;
}

void RunSize(usize count)
{
    ComponentRegistry registry;
    REQUIRE(RegisterBench(&registry).Ok());
    ecs::World world;
    KeyIndex index;
    Vector<ecs::Entity> made;
    BenchPopulate(world, index, &made, count);

    // The memory store.
    {
        MemoryStore store;
        WorldStorage storage(registry, store);
        u64 t = os::MonotonicTimeNanos();
        REQUIRE(storage.Save(world, index, SaveOptions(), nullptr).Ok());
        const u64 save = Ms(t);
        ecs::World loaded;
        KeyIndex loaded_index;
        t = os::MonotonicTimeNanos();
        REQUIRE(storage.Load(loaded, loaded_index, LoadOptions(), nullptr).Ok());
        os::Print("bench %zu entities, memory store: save %llu ms, load %llu ms\n", count,
                  static_cast<unsigned long long>(save), static_cast<unsigned long long>(Ms(t)));
    }
    for (const int columns : {0, 1})
    {
        database_test::TempDir dir;
        const io::Path path = dir / StringView("bench.db");
        database::OpenOptions options;
        options.location = path.View();
        database::SqliteDatabase db;
        UniquePtr<database::Connection> connection;
        UniquePtr<SqlStore> store;
        REQUIRE(db.Open(options, &connection).Ok());
        const IndexRequest requests[] = {{"bn_actor", "hp"}};
        SqlStoreOptions o;
        o.registry = &registry;
        o.columns = columns != 0;
        o.indexes = columns != 0 ? requests : nullptr;
        o.index_count = columns != 0 ? 1 : 0;
        REQUIRE(SqlStore::Open(*connection, &store, o).Ok());
        WorldStorage storage(registry, *store);
        SaveSession session;
        SaveOptions full;
        full.session = &session;
        u64 t = os::MonotonicTimeNanos();
        REQUIRE(storage.Save(world, index, full, nullptr).Ok());
        const u64 save = Ms(t);
        ecs::World loaded;
        KeyIndex loaded_index;
        t = os::MonotonicTimeNanos();
        REQUIRE(storage.Load(loaded, loaded_index, LoadOptions(), nullptr).Ok());
        const u64 load = Ms(t);
        os::Print("bench %zu entities, SQLite %s: save %llu ms, load %llu ms\n", count,
                  columns != 0 ? "columns+index" : "blob", static_cast<unsigned long long>(save),
                  static_cast<unsigned long long>(load));

        // Incremental: idle, then 1 percent written.
        SaveOptions inc;
        inc.mode = SaveMode::Incremental;
        inc.session = &session;
        world.EndFrame();
        Report report;
        t = os::MonotonicTimeNanos();
        REQUIRE(storage.Save(world, index, inc, &report).Ok());
        os::Print("bench %zu entities, SQLite %s: idle incremental save %llu us (encoded %zu, chunks skipped %zu)\n",
                  count, columns != 0 ? "columns+index" : "blob", static_cast<unsigned long long>(Us(t)),
                  report.encoded, report.chunks_skipped);
        BnRng rng{5};
        for (usize i = 0; i < count / 100; ++i)
        {
            BnActor* a = world.Get<BnActor>(made[rng.Next() % made.Size()]);
            a->hp += 1;
        }
        world.EndFrame();
        t = os::MonotonicTimeNanos();
        REQUIRE(storage.Save(world, index, inc, &report).Ok());
        os::Print("bench %zu entities, SQLite %s: incremental save of ~1 percent %llu ms (encoded %zu, written %zu)\n",
                  count, columns != 0 ? "columns+index" : "blob", static_cast<unsigned long long>(Ms(t)),
                  report.encoded, report.created + report.replaced);
        // The same number of writes, but to entities created together (so they share chunks): what the chunk stamps are
        // good at.
        for (usize i = 0; i < count / 100; ++i)
        {
            world.Get<BnActor>(made[i])->hp += 1;
        }
        world.EndFrame();
        t = os::MonotonicTimeNanos();
        REQUIRE(storage.Save(world, index, inc, &report).Ok());
        os::Print(
            "bench %zu entities, SQLite %s: incremental save of 1 percent in neighbouring entities %llu ms (encoded %zu, written %zu)\n",
            count, columns != 0 ? "columns+index" : "blob", static_cast<unsigned long long>(Ms(t)), report.encoded,
            report.created + report.replaced);
        if (columns != 0)
        {
            usize hits = 0;
            t = os::MonotonicTimeNanos();
            REQUIRE(
                store->CountWhere("bn_actor", "hp", CompareOp::Greater, database::ValueView::Integer(190), &hits).Ok());
            os::Print("bench %zu entities, FindKeys on the indexed field: %llu us (%zu hits)\n", count,
                      static_cast<unsigned long long>(Us(t)), hits);
        }
        store.Reset();
    }

    // The encoding, on 1 and on 8 participants.
    for (const usize participants : {1u, 8u})
    {
        jobs::Scheduler::Config c;
        c.participants = participants;
        c.pin = false;
        jobs::Scheduler scheduler(c);
        MemoryStore store;
        WorldStorage storage(registry, store);
        UniquePtr<AsyncSave> handle;
        SaveOptions options;
        options.batch_size = 16384;
        const u64 t = os::MonotonicTimeNanos();
        REQUIRE(storage.SaveAsync(scheduler, world, index, options, &handle).Ok());
        const u64 encode = Ms(t);
        REQUIRE(handle->Wait(nullptr).Ok());
        os::Print("bench %zu entities, async save with %zu participants: snapshot and encoding %llu ms, all %llu ms\n",
                  count, participants, static_cast<unsigned long long>(encode), static_cast<unsigned long long>(Ms(t)));
    }
}

}  // namespace

TEST_CASE("Storage benchmark: 100,000 entities", "[.][storage-bench]")
{
    RunSize(100000);
}
TEST_CASE("Storage benchmark: 1,000,000 entities", "[.][storage-bench]")
{
    RunSize(1000000);
}

}  // namespace engine_placeholder::storage
