// The SQL store's column layout, bulk writes and field queries (Phase 19 Day 263).

#include <catch2/catch_test_macros.hpp>

#include "database/sqlite/sqlite_database.h"
#include "no_alter_connection.h"
#include "os/console.h"
#include "os/os.h"
#include "storage/builtin_components.h"
#include "storage/memory_store.h"
#include "storage/sql_store.h"
#include "storage/world_storage.h"
#include "store_conformance.h"
#include "temp_dir.h"
#include "world_compare.h"

namespace vse::storage
{
namespace
{

using ecs::Entity;
using ecs::World;

enum class LyMode : u8
{
    Idle = 0,
    Walk = 1,
    Run = 2,
    Dead = 3
};

struct LyActor
{
    i32 hp = 0;
    u16 level = 0;
    u64 tag = 0;
    bool alive = false;
    f32 speed = 0;
    f64 score = 0;
    Vec3 pos;
    Quaternion rot;
    LyMode mode = LyMode::Idle;
    String<HeapAllocator> name{HeapAllocator::Default()};
};
struct LyRefs
{
    ResourceRef mesh;
    u64 clip = 0;
};
struct LyPlain
{
    i32 value = 0;
};

Status RegisterActors(ComponentRegistry* r)
{
    ENGINE_TRY(RegisterBuiltinComponents(r));
    ENGINE_TRY(r->Register<LyActor>(
        {.id = 4000,
         .name = "ly_actor",
         .flags = kComponentStored | kComponentQueryable,
         .fields = {Field<&LyActor::hp>("hp"), Field<&LyActor::level>("level"), Field<&LyActor::tag>("tag"),
                    Field<&LyActor::alive>("alive"), Field<&LyActor::speed>("speed"), Field<&LyActor::score>("score"),
                    Field<&LyActor::pos>("pos"), Field<&LyActor::rot>("rot"),
                    Field<&LyActor::mode>("mode", FieldOptions::Enum(0, 3)), Field<&LyActor::name>("name")}}));
    ENGINE_TRY(r->Register<LyPlain>({.id = 4001, .name = "ly_plain", .fields = {Field<&LyPlain::value>("value")}}));
    return Status::Success();
}

struct LyRng
{
    u64 s;
    u32 Next()
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<u32>(s >> 33);
    }
};

void PopulateActors(World& world, KeyIndex& index, usize count, u64 seed)
{
    LyRng rng{seed};
    KeyGenerator keys = KeyGenerator::Seeded(seed);
    for (usize i = 0; i < count; ++i)
    {
        const Entity e = world.Create(EntityKeyComponent{keys.Next()});
        REQUIRE(e.IsValid());
        if (rng.Next() % 4 != 0)
        {
            LyActor a;
            a.hp = static_cast<i32>(rng.Next() % 200) - 50;
            a.level = static_cast<u16>(rng.Next() % 60);
            a.tag = (static_cast<u64>(rng.Next()) << 32) | rng.Next();
            a.alive = (rng.Next() & 1) != 0;
            a.speed = static_cast<f32>(rng.Next() % 1000) / 8.0f;
            a.score = static_cast<f64>(rng.Next() % 100000) / 3.0;
            a.pos = Vec3(static_cast<f32>(rng.Next() % 100) - 50.0f, static_cast<f32>(rng.Next() % 100), -1.5f);
            a.rot = Quaternion();
            a.mode = static_cast<LyMode>(rng.Next() % 4);
            a.name.Append(static_cast<char>('a' + rng.Next() % 26));
            REQUIRE(world.Add(e, Move(a)) == ecs::Status::Ok);
        }
        if (rng.Next() % 3 == 0)
        {
            REQUIRE(world.Add(e, LyPlain{static_cast<i32>(rng.Next())}) == ecs::Status::Ok);
        }
    }
    REQUIRE(index.Sync(world).Ok());
}

struct DbRig
{
    database::SqliteDatabase db;
    UniquePtr<database::Connection> connection;
    UniquePtr<SqlStore> store;
    void Open(const ComponentRegistry& registry, bool columns,
              const database::OpenOptions& options = database::OpenOptions())
    {
        store.Reset();
        if (!connection)
        {
            REQUIRE(db.Open(options, &connection).Ok());
        }
        SqlStoreOptions o;
        o.registry = &registry;
        o.columns = columns;
        REQUIRE(SqlStore::Open(*connection, &store, o).Ok());
    }
};

usize Mismatches(const SqlStore& store)
{
    usize m = ~usize(0);
    REQUIRE(store.VerifyColumns(&m).Ok());
    return m;
}

}  // namespace

TEST_CASE("SQL layout: a world saved with and without columns loads identically, and the columns match the blobs",
          "[storage][store][sql][layout]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterActors(&registry).Ok());
    World source;
    KeyIndex index;
    PopulateActors(source, index, 3000, 5);
    for (const bool columns : {false, true})
    {
        DbRig rig;
        rig.Open(registry, columns);
        WorldStorage storage(registry, *rig.store);
        REQUIRE(storage.Save(source, index, SaveOptions(), nullptr).Ok());
        World loaded;
        KeyIndex loaded_index;
        REQUIRE(storage.Load(loaded, loaded_index, LoadOptions(), nullptr).Ok());
        test::RequireSameWorlds(registry, source, loaded);
        if (columns)
        {
            CHECK(Mismatches(*rig.store) == 0);
        }
        else
        {
            usize m = 0;
            CHECK(rig.store->VerifyColumns(&m).Is(ErrorCode::Unsupported));
        }
    }
}

namespace
{

enum class Col
{
    Hp,
    Level,
    Tag,
    Alive,
    Speed,
    Score,
    PosX,
    PosY,
    PosZ,
    RotW,
    Mode
};

struct ColInfo
{
    const char* name;
    Col col;
    bool real;
};
const ColInfo kColumns[] = {{"hp", Col::Hp, false},       {"level", Col::Level, false}, {"tag", Col::Tag, false},
                            {"alive", Col::Alive, false}, {"speed", Col::Speed, true},  {"score", Col::Score, true},
                            {"pos_x", Col::PosX, true},   {"pos_y", Col::PosY, true},   {"pos_z", Col::PosZ, true},
                            {"rot_w", Col::RotW, true},   {"mode", Col::Mode, false}};

i64 IntOf(const LyActor& a, Col c)
{
    switch (c)
    {
    case Col::Hp: return a.hp;
    case Col::Level: return a.level;
    case Col::Tag: return static_cast<i64>(a.tag);
    case Col::Alive: return a.alive ? 1 : 0;
    case Col::Mode: return static_cast<i64>(a.mode);
    default: return 0;
    }
}
f64 RealOf(const LyActor& a, Col c)
{
    switch (c)
    {
    case Col::Speed: return static_cast<f64>(a.speed);
    case Col::Score: return a.score;
    case Col::PosX: return static_cast<f64>(a.pos.x);
    case Col::PosY: return static_cast<f64>(a.pos.y);
    case Col::PosZ: return static_cast<f64>(a.pos.z);
    case Col::RotW: return static_cast<f64>(a.rot.w);
    default: return 0;
    }
}
template <class T> bool Compare(T a, CompareOp op, T b)
{
    switch (op)
    {
    case CompareOp::Equal: return a == b;
    case CompareOp::NotEqual: return a != b;
    case CompareOp::Less: return a < b;
    case CompareOp::LessOrEqual: return a <= b;
    case CompareOp::Greater: return a > b;
    case CompareOp::GreaterOrEqual: return a >= b;
    }
    return false;
}

}  // namespace

TEST_CASE(
    "SQL layout: FindKeys equals a brute-force scan of the world for every operator and column over 10,000 entities",
    "[storage][store][sql][layout]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterActors(&registry).Ok());
    World world;
    KeyIndex index;
    PopulateActors(world, index, 10000, 11);
    DbRig rig;
    rig.Open(registry, true);
    WorldStorage storage(registry, *rig.store);
    REQUIRE(storage.Save(world, index, SaveOptions(), nullptr).Ok());
    CHECK(Mismatches(*rig.store) == 0);

    // The actors, sorted by key, and a probe value per column taken from one of them.
    struct Row
    {
        EntityKey key;
        LyActor actor;
    };
    Vector<Row> rows;
    world.Query<ecs::Read<EntityKeyComponent>, ecs::Read<LyActor>>().ForEach(
        [&](Entity, const EntityKeyComponent& k, const LyActor& a)
        {
            Row r;
            r.key = k.key;
            r.actor.hp = a.hp;
            r.actor.level = a.level;
            r.actor.tag = a.tag;
            r.actor.alive = a.alive;
            r.actor.speed = a.speed;
            r.actor.score = a.score;
            r.actor.pos = a.pos;
            r.actor.rot = a.rot;
            r.actor.mode = a.mode;
            rows.Push(Move(r));
        });
    REQUIRE(rows.Size() > 5000);
    usize checked = 0;
    for (const ColInfo& column : kColumns)
    {
        const LyActor& probe = rows[rows.Size() / 3].actor;
        for (const CompareOp op : {CompareOp::Equal, CompareOp::NotEqual, CompareOp::Less, CompareOp::LessOrEqual,
                                   CompareOp::Greater, CompareOp::GreaterOrEqual})
        {
            Vector<EntityKey> expected;
            for (usize i = 0; i < rows.Size(); ++i)
            {
                const bool hit = column.real ? Compare(RealOf(rows[i].actor, column.col), op, RealOf(probe, column.col))
                                             : Compare(IntOf(rows[i].actor, column.col), op, IntOf(probe, column.col));
                if (hit)
                {
                    expected.Push(rows[i].key);
                }
            }
            // Keys come back in key order.
            for (usize i = 1; i < expected.Size(); ++i)
            {
                for (usize j = i; j > 0 && expected[j] < expected[j - 1]; --j)
                {
                    const EntityKey t = expected[j];
                    expected[j] = expected[j - 1];
                    expected[j - 1] = t;
                }
            }
            Vector<EntityKey> found;
            const database::ValueView value = column.real ? database::ValueView::Real(RealOf(probe, column.col))
                                                          : database::ValueView::Integer(IntOf(probe, column.col));
            REQUIRE(rig.store->FindKeys("ly_actor", column.name, op, value, &found).Ok());
            INFO(column.name << " op " << static_cast<int>(op));
            REQUIRE(found.Size() == expected.Size());
            for (usize i = 0; i < found.Size(); ++i)
            {
                REQUIRE(found[i] == expected[i]);
            }
            usize count = 0;
            REQUIRE(rig.store->CountWhere("ly_actor", column.name, op, value, &count).Ok());
            CHECK(count == expected.Size());
            ++checked;
        }
    }
    CHECK(checked == 66);

    // What cannot be asked.
    Vector<EntityKey> none;
    const database::ValueView one = database::ValueView::Integer(1);
    CHECK(rig.store->FindKeys("ly_plain", "value", CompareOp::Equal, one, &none)
              .Is(ErrorCode::Unsupported));  // registered, not queryable
    CHECK(rig.store->FindKeys("nobody", "value", CompareOp::Equal, one, &none).Is(ErrorCode::NotFound));
    CHECK(rig.store->FindKeys("ly_actor", "name", CompareOp::Equal, one, &none)
              .Is(ErrorCode::NotFound));  // a string stays in the blob
    CHECK(rig.store->FindKeys("ly_actor", "hp", CompareOp::Equal, database::ValueView::Real(1.0), &none)
              .Is(ErrorCode::Invalid));
    // A store without columns says so.
    DbRig plain;
    plain.Open(registry, false);
    CHECK(plain.store->FindKeys("ly_actor", "hp", CompareOp::Equal, one, &none).Is(ErrorCode::Unsupported));
}

TEST_CASE(
    "SQL layout: rows follow replacement, removal and a record without the component, with and without enforced foreign keys",
    "[storage][store][sql][layout]")
{
    for (const bool foreign_keys : {true, false})
    {
        ComponentRegistry registry;
        REQUIRE(RegisterActors(&registry).Ok());
        World world;
        KeyIndex index;
        PopulateActors(world, index, 400, 17);
        DbRig rig;
        database::OpenOptions open_options;
        open_options.foreign_keys = foreign_keys;
        rig.Open(registry, true, open_options);
        WorldStorage storage(registry, *rig.store);
        SaveSession session;
        SaveOptions options;
        options.mode = SaveMode::Incremental;
        options.session = &session;
        REQUIRE(storage.Save(world, index, options, nullptr).Ok());
        CHECK(Mismatches(*rig.store) == 0);
        // Change some actors, strip the component from some, destroy some, then save the changes only.
        world.EndFrame();
        usize n = 0;
        Vector<Entity> actors;
        world.Query<ecs::Read<LyActor>>().ForEach([&](Entity e, const LyActor&) { actors.Push(e); });
        for (usize i = 0; i < actors.Size(); ++i, ++n)
        {
            if (n % 3 == 0)
            {
                LyActor a;
                a.hp = static_cast<i32>(n);
                REQUIRE(world.Set(actors[i], Move(a)) == ecs::Status::Ok);
            }
            else if (n % 3 == 1)
            {
                REQUIRE(world.Remove<LyActor>(actors[i]) == ecs::Status::Ok);
            }
            else if (n % 9 == 2)
            {
                REQUIRE(world.Destroy(actors[i]) == ecs::Status::Ok);
            }
        }
        Report report;
        REQUIRE(storage.Save(world, index, options, &report).Ok());
        CHECK(report.reconciled == 0);
        CHECK(Mismatches(*rig.store) == 0);
    }
}

TEST_CASE("SQL layout: switching the layout off and on refills the tables from the blobs",
          "[storage][store][sql][layout]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterActors(&registry).Ok());
    World world;
    KeyIndex index;
    PopulateActors(world, index, 300, 23);
    DbRig rig;
    rig.Open(registry, true);
    {
        WorldStorage storage(registry, *rig.store);
        REQUIRE(storage.Save(world, index, SaveOptions(), nullptr).Ok());
    }
    rig.Open(registry, false);  // writes while the layout is off
    {
        WorldStorage storage(registry, *rig.store);
        World more;
        KeyIndex more_index;
        PopulateActors(more, more_index, 50, 99);
        REQUIRE(storage.Save(more, more_index, SaveOptions(), nullptr)
                    .Ok());  // a full save: replaces the 300 with these 50
    }
    rig.Open(registry, true);
    CHECK(Mismatches(*rig.store) == 0);
    usize rows = 0;
    CHECK(rig.store->CountWhere("ly_actor", "hp", CompareOp::GreaterOrEqual, database::ValueView::Integer(-1000), &rows)
              .Ok());
    CHECK(rows <= 50);
}

namespace
{

struct LyStatV1
{
    i32 hp = 0;
    f32 speed = 0;
};
struct LyStatV2
{
    i32 hp = 0;
    f32 speed = 0;
    i32 mana = 0;
};

Status StatV1ToV2(bytes::ByteReader& in, bytes::ByteWriter& out, const CodecLimits&)
{
    i32 hp = 0;
    f32 speed = 0;
    bytes::ByteError e = in.ReadI32(&hp);
    if (e == bytes::ByteError::Ok)
    {
        e = in.ReadF32(&speed);
    }
    if (e != bytes::ByteError::Ok)
    {
        return detail::FromByteError(e);
    }
    out.WriteI32(hp);
    out.WriteF32(speed);
    out.WriteI32(7);  // mana did not exist: the default of the upgrade
    return Status::Success();
}

}  // namespace

TEST_CASE("SQL layout: a field added to a queryable component updates the table, with and without ALTER",
          "[storage][store][sql][layout]")
{
    for (const bool can_alter : {true, false})
    {
        database::SqliteDatabase db;
        UniquePtr<database::Connection> raw;
        REQUIRE(db.Open(database::OpenOptions(), &raw).Ok());
        test::NoAlterConnection restricted(*raw);
        database::Connection& connection = can_alter ? *raw : static_cast<database::Connection&>(restricted);
        World world;
        KeyIndex index;
        KeyGenerator keys = KeyGenerator::Seeded(4);
        for (u32 i = 0; i < 100; ++i)
        {
            world.Create(EntityKeyComponent{keys.Next()}, LyStatV1{static_cast<i32>(i), static_cast<f32>(i) / 2});
        }
        REQUIRE(index.Sync(world).Ok());
        {
            ComponentRegistry v1;
            REQUIRE(RegisterBuiltinComponents(&v1).Ok());
            REQUIRE(v1.Register<LyStatV1>({.id = 4100,
                                           .name = "ly_stat",
                                           .flags = kComponentStored | kComponentQueryable,
                                           .fields = {Field<&LyStatV1::hp>("hp"), Field<&LyStatV1::speed>("speed")}})
                        .Ok());
            SqlStoreOptions o;
            o.registry = &v1;
            o.columns = true;
            UniquePtr<SqlStore> store;
            REQUIRE(SqlStore::Open(connection, &store, o).Ok());
            WorldStorage storage(v1, *store);
            REQUIRE(storage.Save(world, index, SaveOptions(), nullptr).Ok());
            CHECK(Mismatches(*store) == 0);
        }
        ComponentRegistry v2;
        REQUIRE(RegisterBuiltinComponents(&v2).Ok());
        REQUIRE(v2.Register<LyStatV2>({.id = 4100,
                                       .name = "ly_stat",
                                       .version = 2,
                                       .flags = kComponentStored | kComponentQueryable,
                                       .fields = {Field<&LyStatV2::hp>("hp"), Field<&LyStatV2::speed>("speed"),
                                                  Field<&LyStatV2::mana>("mana")},
                                       .upgrades = {{1, &StatV1ToV2}}})
                    .Ok());
        SqlStoreOptions o;
        o.registry = &v2;
        o.columns = true;
        UniquePtr<SqlStore> store;
        REQUIRE(SqlStore::Open(connection, &store, o).Ok());
        CHECK(Mismatches(*store) == 0);
        usize with_mana = 0;
        REQUIRE(
            store->CountWhere("ly_stat", "mana", CompareOp::Equal, database::ValueView::Integer(7), &with_mana).Ok());
        CHECK(with_mana == 100);  // the old rows got the upgrade's value from their blobs
        usize fast = 0;
        REQUIRE(store->CountWhere("ly_stat", "hp", CompareOp::Less, database::ValueView::Integer(10), &fast).Ok());
        CHECK(fast == 10);
    }
}

TEST_CASE("SQL layout: PutMany equals the same records put one by one, for every group size",
          "[storage][store][sql][layout]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterActors(&registry).Ok());
    for (const usize n : {usize(1), usize(15), usize(16), usize(17), usize(32), usize(33), usize(100)})
    {
        DbRig many, single;
        many.Open(registry, false);
        single.Open(registry, false);
        Vector<EntityRecord> records;
        for (u64 k = 1; k <= n; ++k)
        {
            records.Push(conformance::Rec(k * 7, static_cast<u8>(k), k % 13));
        }
        REQUIRE(many.store->Begin().Ok());
        REQUIRE(many.store->PutMany(Span<const EntityRecord>(records.Data(), records.Size())).Ok());
        REQUIRE(many.store->PutMany(Span<const EntityRecord>(records.Data(), records.Size())).Ok());  // again: replaces
        REQUIRE(many.store->Commit().Ok());
        for (usize i = 0; i < records.Size(); ++i)
        {
            REQUIRE(single.store->Put(records[i]).Ok());
        }
        usize a = 0, b = 0;
        REQUIRE(many.store->Count(&a).Ok());
        REQUIRE(single.store->Count(&b).Ok());
        CHECK(a == n);
        CHECK(b == n);
        CHECK(conformance::Fingerprint(*many.store) == conformance::Fingerprint(*single.store));
    }
}

TEST_CASE("SQL layout: an indexed column answers the same query", "[storage][store][sql][layout]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterActors(&registry).Ok());
    World world;
    KeyIndex index;
    PopulateActors(world, index, 2000, 41);
    Vector<EntityKey> plain_hits, indexed_hits;
    for (const bool indexed : {false, true})
    {
        DbRig rig;
        const IndexRequest requests[] = {{"ly_actor", "hp"}, {"ly_actor", "pos_x"}};
        SqlStoreOptions o;
        o.registry = &registry;
        o.columns = true;
        o.indexes = indexed ? requests : nullptr;
        o.index_count = indexed ? 2 : 0;
        REQUIRE(rig.db.Open(database::OpenOptions(), &rig.connection).Ok());
        REQUIRE(SqlStore::Open(*rig.connection, &rig.store, o).Ok());
        WorldStorage storage(registry, *rig.store);
        REQUIRE(storage.Save(world, index, SaveOptions(), nullptr).Ok());
        REQUIRE(rig.store
                    ->FindKeys("ly_actor", "hp", CompareOp::Greater, database::ValueView::Integer(100),
                               indexed ? &indexed_hits : &plain_hits)
                    .Ok());
        if (indexed)
        {
            // Opening again with the same requests changes nothing; a request for a column that does not exist is
            // refused.
            rig.store.Reset();
            REQUIRE(SqlStore::Open(*rig.connection, &rig.store, o).Ok());
            const IndexRequest bad[] = {{"ly_actor", "name"}};
            o.indexes = bad;
            o.index_count = 1;
            rig.store.Reset();
            CHECK(SqlStore::Open(*rig.connection, &rig.store, o).Is(ErrorCode::Invalid));
        }
    }
    REQUIRE(plain_hits.Size() == indexed_hits.Size());
    CHECK(plain_hits.Size() > 0);
    for (usize i = 0; i < plain_hits.Size(); ++i)
    {
        CHECK(plain_hits[i] == indexed_hits[i]);
    }
}

TEST_CASE("Benchmark: 100,000 entities saved and loaded through the SQL store, blob only and with columns",
          "[storage][store][sql][layout][benchmark]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterActors(&registry).Ok());
    World world;
    KeyIndex index;
    PopulateActors(world, index, 100000, 31);
    for (const int mode : {0, 1, 2})
    {  // blob only; columns; columns with an index on hp
        const bool columns = mode != 0;
        database_test::TempDir dir;
        const io::Path path = dir / StringView("bench.db");
        database::OpenOptions options;
        options.location = path.View();
        DbRig rig;
        {
            const IndexRequest requests[] = {{"ly_actor", "hp"}};
            SqlStoreOptions o;
            o.registry = &registry;
            o.columns = columns;
            o.indexes = mode == 2 ? requests : nullptr;
            o.index_count = mode == 2 ? 1 : 0;
            REQUIRE(rig.db.Open(options, &rig.connection).Ok());
            REQUIRE(SqlStore::Open(*rig.connection, &rig.store, o).Ok());
        }
        WorldStorage storage(registry, *rig.store);
        const u64 t0 = os::MonotonicTimeNanos();
        REQUIRE(storage.Save(world, index, SaveOptions(), nullptr).Ok());
        const u64 t1 = os::MonotonicTimeNanos();
        World loaded;
        KeyIndex loaded_index;
        REQUIRE(storage.Load(loaded, loaded_index, LoadOptions(), nullptr).Ok());
        const u64 t2 = os::MonotonicTimeNanos();
        usize found = 0;
        u64 query_us = 0;
        if (columns)
        {
            const u64 q0 = os::MonotonicTimeNanos();
            REQUIRE(
                rig.store->CountWhere("ly_actor", "hp", CompareOp::Greater, database::ValueView::Integer(100), &found)
                    .Ok());
            query_us = (os::MonotonicTimeNanos() - q0) / 1000;
        }
        os::Print("sql store, 100000 entities, %s: save %llu ms, load %llu ms, query hp > 100 %llu us (%zu hits)\n",
                  mode == 0 ? "blob only" : (mode == 1 ? "with columns" : "with columns and an index on hp"),
                  static_cast<unsigned long long>((t1 - t0) / 1000000),
                  static_cast<unsigned long long>((t2 - t1) / 1000000), static_cast<unsigned long long>(query_us),
                  found);
        rig.store.Reset();
    }
}

TEST_CASE("SQL layout: a resource reference is an integer column, queryable like the u64 it replaced",
          "[storage][store][sql][layout][reference]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterBuiltinComponents(&registry).Ok());
    REQUIRE(registry
                .Register<LyRefs>({.id = 4002,
                                   .name = "ly_refs",
                                   .flags = kComponentStored | kComponentQueryable,
                                   .fields = {Field<&LyRefs::mesh>("mesh"),
                                              Field<&LyRefs::clip>("clip", FieldOptions::Reference(0, true))}})
                .Ok());
    World world;
    KeyIndex index;
    KeyGenerator keys = KeyGenerator::Seeded(5);
    EntityKey wanted;
    for (u64 i = 1; i <= 5; ++i)
    {
        const Entity e = world.Create(EntityKeyComponent{keys.Next()});
        LyRefs refs;
        refs.mesh.id = i * 100;
        refs.clip = i * 1000;
        REQUIRE(world.Add(e, Move(refs)) == ecs::Status::Ok);
        if (i == 3)
        {
            wanted = world.Get<EntityKeyComponent>(e)->key;
        }
    }
    REQUIRE(index.Sync(world).Ok());
    DbRig rig;
    rig.Open(registry, true);
    WorldStorage storage(registry, *rig.store);
    REQUIRE(storage.Save(world, index, SaveOptions(), nullptr).Ok());
    CHECK(Mismatches(*rig.store) == 0);
    Vector<EntityKey> found;
    REQUIRE(rig.store->FindKeys("ly_refs", "mesh", CompareOp::Equal, database::ValueView::Integer(300), &found).Ok());
    REQUIRE(found.Size() == 1);
    CHECK(found[0] == wanted);
    found.Clear();
    REQUIRE(rig.store->FindKeys("ly_refs", "clip", CompareOp::Equal, database::ValueView::Integer(3000), &found).Ok());
    REQUIRE(found.Size() == 1);
    CHECK(found[0] == wanted);
}

}  // namespace vse::storage
