// Hostile database files (Phase 19 Day 265): a deterministic corruption fuzz over a saved SQLite file through the open
// and load path. Every case ends in a Status (the open, the store open or the load fails, or the load succeeds with a
// world that has valid components); none crashes or hangs.

#include <catch2/catch_test_macros.hpp>

#include "database/sqlite/sqlite_database.h"
#include "ecs/transform.h"
#include "io/file.h"
#include "io/file_system.h"
#include "storage/builtin_components.h"
#include "storage/component_record.h"
#include "storage/memory_store.h"
#include "storage/sql_store.h"
#include "storage/world_storage.h"
#include "temp_dir.h"

namespace engine_placeholder::storage
{
namespace
{

struct HoActor
{
    i32 hp = 0;
    String<HeapAllocator> name{HeapAllocator::Default()};
    Vec3 pos;
};

Status RegisterHostile(ComponentRegistry* r)
{
    ENGINE_TRY(RegisterBuiltinComponents(r));
    return r->Register<HoActor>(
        {.id = 6000,
         .name = "ho_actor",
         .flags = kComponentStored | kComponentQueryable,
         .fields = {Field<&HoActor::hp>("hp"), Field<&HoActor::name>("name"), Field<&HoActor::pos>("pos")}});
}

struct HoRng
{
    u64 s;
    u32 Next()
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<u32>(s >> 33);
    }
};

// Opens `path`, the store and loads; returns how far it got (0 open failed, 1 store failed, 2 load failed, 3 loaded).
int TryLoad(const ComponentRegistry& registry, const io::Path& path, database::IntegrityCheck check,
            const CodecLimits& limits, usize* loaded)
{
    database::SqliteDatabase db;
    database::OpenOptions options;
    options.location = path.View();
    options.mode = database::OpenMode::ReadWrite;
    options.journal_mode = database::JournalMode::Delete;
    options.integrity_check = check;
    UniquePtr<database::Connection> connection;
    if (db.Open(options, &connection).Failed())
    {
        return 0;
    }
    UniquePtr<SqlStore> store;
    SqlStoreOptions so;
    so.registry = &registry;
    so.columns = true;
    so.limits = limits;
    if (SqlStore::Open(*connection, &store, so).Failed())
    {
        return 1;
    }
    WorldStorage storage(registry, *store);
    storage.SetLimits(limits);
    ecs::World world;
    KeyIndex index;
    LoadOptions load;
    load.unknown = UnknownPolicy::KeepOpaque;
    load.missing = MissingPolicy::NullReference;
    if (storage.Load(world, index, load, nullptr).Failed())
    {
        return 2;
    }
    usize n = 0;
    world.Query<ecs::Read<EntityKeyComponent>>().ForEach([&](ecs::Entity, const EntityKeyComponent&) { ++n; });
    *loaded = n;
    usize mismatches = 0;
    static_cast<void>(store->VerifyColumns(&mismatches));  // any answer, as long as it is one
    return 3;
}

bool WriteBytes(const io::Path& path, const Vector<u8>& bytes)
{
    return io::WriteFile(path, bytes.Data(), bytes.Size()).Ok();
}

}  // namespace

TEST_CASE("Hostile files: a corruption fuzz over a saved database always ends in a Status", "[storage][hostile]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterHostile(&registry).Ok());
    database_test::TempDir dir;
    const io::Path good = dir / StringView("good.db");
    {
        database::SqliteDatabase db;
        database::OpenOptions options;
        options.location = good.View();
        options.journal_mode = database::JournalMode::Delete;
        UniquePtr<database::Connection> connection;
        REQUIRE(db.Open(options, &connection).Ok());
        UniquePtr<SqlStore> store;
        SqlStoreOptions so;
        so.registry = &registry;
        so.columns = true;
        REQUIRE(SqlStore::Open(*connection, &store, so).Ok());
        ecs::World world;
        KeyIndex index;
        KeyGenerator keys = KeyGenerator::Seeded(8);
        HoRng rng{3};
        for (u32 i = 0; i < 150; ++i)
        {
            const ecs::Entity e = world.Create(EntityKeyComponent{keys.Next()});
            HoActor a;
            a.hp = static_cast<i32>(rng.Next() % 100);
            a.name.Append("actor");
            a.pos = Vec3(1, 2, 3);
            REQUIRE(world.Add(e, Move(a)) == ecs::Status::Ok);
        }
        WorldStorage storage(registry, *store);
        REQUIRE(storage.Save(world, index, SaveOptions(), nullptr).Ok());
    }
    Vector<u8> original;
    REQUIRE(io::ReadFile(good, &original).Ok());
    REQUIRE(original.Size() >= 8192);
    usize loaded = 0;
    REQUIRE(TryLoad(registry, good, database::IntegrityCheck::Full, CodecLimits(), &loaded) == 3);
    CHECK(loaded == 150);

    const io::Path bad = dir / StringView("bad.db");
    usize counts[4] = {0, 0, 0, 0};
    const database::IntegrityCheck checks[] = {database::IntegrityCheck::Off, database::IntegrityCheck::Quick,
                                               database::IntegrityCheck::Full};
    HoRng rng{1234};
    u32 cases = 0;
    const auto run = [&](const Vector<u8>& bytes)
    {
        REQUIRE(WriteBytes(bad, bytes));
        const int reached = TryLoad(registry, bad, checks[cases % 3], CodecLimits(), &loaded);
        ++counts[reached];
        ++cases;
    };
    // Byte flips: one to eight bytes of the file, anywhere.
    for (u32 i = 0; i < 160; ++i)
    {
        Vector<u8> m;
        for (usize k = 0; k < original.Size(); ++k)
        {
            m.Push(original[k]);
        }
        for (u32 f = 1 + i % 8; f > 0; --f)
        {
            m[rng.Next() % m.Size()] ^= static_cast<u8>(1u << (rng.Next() % 8));
        }
        run(m);
    }
    // Truncation at many lengths (including zero and one byte short).
    for (u32 i = 0; i < 60; ++i)
    {
        const usize cut = i == 0 ? 0 : (i == 1 ? original.Size() - 1 : rng.Next() % original.Size());
        Vector<u8> m;
        for (usize k = 0; k < cut; ++k)
        {
            m.Push(original[k]);
        }
        run(m);
    }
    // Page swaps and zeroed pages (4096-byte pages; the file may use another size, the damage is the same).
    for (u32 i = 0; i < 40; ++i)
    {
        Vector<u8> m;
        for (usize k = 0; k < original.Size(); ++k)
        {
            m.Push(original[k]);
        }
        const usize pages = original.Size() / 4096;
        const usize a = rng.Next() % pages;
        const usize b = rng.Next() % pages;
        if (i % 2 == 0)
        {
            for (usize k = 0; k < 4096; ++k)
            {
                const u8 t = m[a * 4096 + k];
                m[a * 4096 + k] = m[b * 4096 + k];
                m[b * 4096 + k] = t;
            }
        }
        else
        {
            for (usize k = 0; k < 4096; ++k)
            {
                m[a * 4096 + k] = 0;
            }
        }
        run(m);
    }
    // Not a database: empty, text, random bytes.
    {
        Vector<u8> m;
        run(m);
        const char text[] = "this is a save game, honest";
        for (usize k = 0; k < sizeof(text); ++k)
        {
            m.Push(static_cast<u8>(text[k]));
        }
        run(m);
        Vector<u8> noise;
        for (u32 k = 0; k < 20000; ++k)
        {
            noise.Push(static_cast<u8>(rng.Next()));
        }
        run(noise);
    }
    CHECK(cases == 263);
    CHECK(counts[0] + counts[1] + counts[2] + counts[3] == cases);
    CHECK(counts[0] + counts[1] + counts[2] > 0);  // damage is noticed somewhere
}

TEST_CASE("Hostile files: a valid database of another schema is refused or ignored, never loaded as a world",
          "[storage][hostile]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterHostile(&registry).Ok());
    database_test::TempDir dir;
    const io::Path path = dir / StringView("other.db");
    {
        database::SqliteDatabase db;
        database::OpenOptions options;
        options.location = path.View();
        options.journal_mode = database::JournalMode::Delete;
        UniquePtr<database::Connection> connection;
        REQUIRE(db.Open(options, &connection).Ok());
        UniquePtr<database::Statement> st;
        REQUIRE(connection->Prepare("CREATE TABLE entity (a TEXT, b TEXT)", &st).Ok());
        bool row = false;
        REQUIRE(st->Step(&row).Ok());
        UniquePtr<database::Statement> st2;
        REQUIRE(connection->Prepare("INSERT INTO entity VALUES ('x', 'y')", &st2).Ok());
        REQUIRE(st2->Step(&row).Ok());
    }
    usize loaded = 0;
    const int reached = TryLoad(registry, path, database::IntegrityCheck::Full, CodecLimits(), &loaded);
    CHECK(reached < 3);
}

TEST_CASE("Hostile files: an oversized record is refused by the limit before it is decoded", "[storage][hostile]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterHostile(&registry).Ok());
    database_test::TempDir dir;
    const io::Path path = dir / StringView("big.db");
    {
        database::SqliteDatabase db;
        database::OpenOptions options;
        options.location = path.View();
        UniquePtr<database::Connection> connection;
        REQUIRE(db.Open(options, &connection).Ok());
        UniquePtr<SqlStore> store;
        REQUIRE(SqlStore::Open(*connection, &store).Ok());
        WorldStorage storage(registry, *store);
        ecs::World world;
        KeyIndex index;
        const ecs::Entity e = world.Create(EntityKeyComponent{KeyGenerator::Seeded(1).Next()});
        HoActor a;
        a.hp = 1;
        a.name.Append("small");
        REQUIRE(world.Add(e, Move(a)) == ecs::Status::Ok);
        REQUIRE(storage.Save(world, index, SaveOptions(), nullptr).Ok());
        // A record far larger than any limit, put straight into the store.
        Vector<u8> huge;
        for (u32 i = 0; i < (4u << 20); ++i)
        {
            huge.Push(static_cast<u8>(i));
        }
        REQUIRE(
            store->Put(EntityRecord::Make(KeyGenerator::Seeded(2).Next(), ByteSpan(huge.Data(), huge.Size()))).Ok());
    }
    CodecLimits limits;
    limits.max_record_bytes = 1 << 20;
    usize loaded = 0;
    const int reached = TryLoad(registry, path, database::IntegrityCheck::Off, limits, &loaded);
    CHECK((reached == 1 ||
           reached == 2));  // refused when the column tables are refilled at open, or by the load; never loaded
}

TEST_CASE("Hostile files: a stored record naming a structural component is refused, not loaded", "[storage][hostile]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterHostile(&registry).Ok());
    for (const u32 id : {static_cast<u32>(kEntityKeyId), static_cast<u32>(kOpaqueComponentsId)})
    {
        MemoryStore store;
        bytes::ByteWriter blob;
        blob.WriteU32(1);
        ComponentRecord record;
        record.id = id;
        record.version = 1;
        REQUIRE(WriteComponentRecord(record, CodecLimits(), blob).Ok());
        REQUIRE(store.Put(EntityRecord::Make(KeyGenerator::Seeded(9).Next(), blob.Bytes())).Ok());
        WorldStorage storage(registry, store);
        ecs::World world;
        KeyIndex index;
        const Status loaded = storage.Load(world, index, LoadOptions(), nullptr);
        REQUIRE(loaded.Failed());
        CHECK(loaded.Code() == ErrorCode::Corrupt);
    }
}

}  // namespace engine_placeholder::storage
