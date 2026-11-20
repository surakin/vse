// The SQL record store (Phase 19 Day 262): the conformance suite on SQLite in memory and in a file, schema migration,
// and crash safety.

#include "database/sqlite/sqlite_database.h"
#include "database/sqlite/sqlite_os.h"
#include "ecs/transform.h"
#include "no_alter_connection.h"
#include "old_schema.h"
#include "storage/builtin_components.h"
#include "storage/save_session.h"
#include "storage/world_storage.h"
#include "world_compare.h"
#include "storage/memory_store.h"
#include "storage/sql_store.h"
#include "sync/thread.h"
#include "store_conformance.h"
#include "temp_dir.h"

namespace vse::storage
{
namespace
{

// A store on a private in-memory database.
struct SqlMemoryFixture
{
    static constexpr bool kInjectsFailure = true;
    static constexpr bool kCanCorrupt = true;
    static constexpr bool kReadersSeeCommitted = false;
    static const char* Name() { return "sql memory"; }
    SqlMemoryFixture()
    {
        REQUIRE(db_.Open(database::OpenOptions(), &connection_).Ok());
        REQUIRE(SqlStore::Open(*connection_, &store_).Ok());
    }
    ~SqlMemoryFixture()
    {
        store_.Reset();
        connection_.Reset();
    }
    Store& Db() { return *store_; }
    void FailAfter(u32 n) { store_->FailAfter(n); }
    bool Corrupt(EntityKey key)
    {
        // Damages the stored bytes behind the store's back (one more byte, the stored hash stays).
        UniquePtr<database::Statement> st;
        if (connection_->Prepare("UPDATE entity SET data = ? WHERE key = ?", &st).Failed())
        {
            return false;
        }
        byte k[16];
        for (usize i = 0; i < 8; ++i)
        {
            k[i] = static_cast<byte>((key.high >> (56 - 8 * i)) & 0xFF);
            k[8 + i] = static_cast<byte>((key.low >> (56 - 8 * i)) & 0xFF);
        }
        const byte junk[3] = {1, 2, 3};
        bool row = false;
        return st->Bind(1, database::ValueView::Blob(ByteSpan(junk, 3))).Ok() &&
               st->Bind(2, database::ValueView::Blob(ByteSpan(k, 16))).Ok() && st->Step(&row).Ok() &&
               connection_->ChangedRows() == 1;
    }

protected:
    database::SqliteDatabase db_;
    UniquePtr<database::Connection> connection_;
    UniquePtr<SqlStore> store_;
};

STORE_CONFORMANCE_CASES(SqlMemoryFixture)

// A store on a file.
class FileStore
{
public:
    FileStore() { Open(); }
    ~FileStore() { Close(); }
    void Open()
    {
        const io::Path path = dir_ / StringView("store.db");
        database::OpenOptions options;
        options.location = path.View();
        REQUIRE(db_.Open(options, &connection_).Ok());
        REQUIRE(SqlStore::Open(*connection_, &store_).Ok());
    }
    void Close()
    {
        store_.Reset();
        connection_.Reset();
    }
    SqlStore& Store() { return *store_; }
    database::Connection& Connection() { return *connection_; }
    io::Path FilePath() const { return dir_ / StringView("store.db"); }

private:
    database_test::TempDir dir_;
    database::SqliteDatabase db_;
    UniquePtr<database::Connection> connection_;
    UniquePtr<SqlStore> store_;
};

struct SqlFileStoreFixture
{
    static constexpr bool kInjectsFailure = true;
    static constexpr bool kCanCorrupt = false;
    static constexpr bool kReadersSeeCommitted = false;
    static const char* Name() { return "sql file"; }
    Store& Db() { return file_.Store(); }
    void FailAfter(u32 n) { file_.Store().FailAfter(n); }
    bool Corrupt(EntityKey) { return false; }

private:
    FileStore file_;
};

STORE_CONFORMANCE_CASES(SqlFileStoreFixture)

Vector<EntityRecord> Records(u64 count)
{
    Vector<EntityRecord> out;
    for (u64 k = 1; k <= count; ++k)
    {
        out.Push(conformance::Rec(k * 3, static_cast<u8>(k), 10 + k % 7));
    }
    return out;
}

}  // namespace

TEST_CASE("SqlStore: a version-1 database is migrated and keeps every record", "[storage][store][sql]")
{
    database_test::TempDir dir;
    const io::Path path = dir / StringView("old.db");
    const Vector<EntityRecord> records = Records(50);
    database::SqliteDatabase db;
    database::OpenOptions options;
    options.location = path.View();
    {
        UniquePtr<database::Connection> c;
        REQUIRE(db.Open(options, &c).Ok());
        REQUIRE(test::BuildSchemaOne(*c, records).Ok());
    }
    for (int round = 0; round < 2; ++round)
    {  // the second open finds the new schema and migrates nothing
        UniquePtr<database::Connection> c;
        REQUIRE(db.Open(options, &c).Ok());
        UniquePtr<SqlStore> store;
        REQUIRE(SqlStore::Open(*c, &store).Ok());
        i64 version = 0;
        REQUIRE(c->SchemaVersion(&version).Ok());
        CHECK(version == SqlStore::kSchemaVersion);
        usize count = 0;
        REQUIRE(store->Count(&count).Ok());
        CHECK(count == 50 + static_cast<usize>(round));
        for (usize i = 0; i < records.Size(); ++i)
        {
            EntityRecord got;
            REQUIRE(store->Get(records[i].key, &got).Ok());
            CHECK(got.hash == records[i].hash);
            CHECK(got.blob.Size() == records[i].blob.Size());
        }
        String<HeapAllocator> value(HeapAllocator::Default());
        REQUIRE(store->GetMetadata(metadata::kFormatVersion, &value).Ok());
        CHECK(value.View() == StringView("1"));
        REQUIRE(store->GetMetadata(metadata::kSchemaVersion, &value).Ok());
        CHECK(value.View() == StringView("3"));
        REQUIRE(store->Put(conformance::Rec(1000, 1)).Ok());  // and the new layout takes writes
    }
}

TEST_CASE("SqlStore: the migration also works on a backend that cannot add a column", "[storage][store][sql]")
{
    database::SqliteDatabase db;
    for (const bool can_alter : {true, false})
    {
        UniquePtr<database::Connection> raw;
        REQUIRE(db.Open(database::OpenOptions(), &raw).Ok());
        const Vector<EntityRecord> records = Records(40);
        REQUIRE(test::BuildSchemaOne(*raw, records).Ok());
        test::NoAlterConnection restricted(*raw);
        database::Connection& c = can_alter ? *raw : static_cast<database::Connection&>(restricted);
        CHECK(c.Supports(database::Feature::AddColumn) == can_alter);
        UniquePtr<SqlStore> store;
        REQUIRE(SqlStore::Open(c, &store).Ok());
        usize count = 0;
        REQUIRE(store->Count(&count).Ok());
        CHECK(count == 40);
        for (usize i = 0; i < records.Size(); ++i)
        {
            EntityRecord got;
            REQUIRE(store->Get(records[i].key, &got).Ok());
            CHECK(got.hash == records[i].hash);
        }
        REQUIRE(store->Put(conformance::Rec(5000, 1)).Ok());
        i64 version = 0;
        REQUIRE(c.SchemaVersion(&version).Ok());
        CHECK(version == SqlStore::kSchemaVersion);
        store.Reset();
    }
}

TEST_CASE("SqlStore: a database with a newer schema is refused and left untouched", "[storage][store][sql]")
{
    database_test::TempDir dir;
    const io::Path path = dir / StringView("new.db");
    database::SqliteDatabase db;
    database::OpenOptions options;
    options.location = path.View();
    UniquePtr<database::Connection> c;
    REQUIRE(db.Open(options, &c).Ok());
    REQUIRE(test::BuildSchemaOne(*c, Records(5)).Ok());
    REQUIRE(c->WriteSchemaVersion(SqlStore::kSchemaVersion + 1).Ok());
    UniquePtr<SqlStore> store;
    CHECK(SqlStore::Open(*c, &store).Is(ErrorCode::TooNew));
    CHECK_FALSE(store);
    i64 version = 0;
    REQUIRE(c->SchemaVersion(&version).Ok());
    CHECK(version == SqlStore::kSchemaVersion + 1);
    CHECK_FALSE(c->InTransaction());
    UniquePtr<database::Statement> st;
    REQUIRE(c->Prepare("SELECT count(*) FROM entity", &st).Ok());  // the version-1 table is still there
    bool row = false;
    REQUIRE(st->Step(&row).Ok());
    CHECK(st->Column(0).AsInteger() == 5);
}

TEST_CASE("SqlStore: a store reopened on its file has what was committed", "[storage][store][sql]")
{
    FileStore file;
    REQUIRE(file.Store().Put(conformance::Rec(7, 1)).Ok());
    REQUIRE(file.Store().SetMetadata("x", "y").Ok());
    file.Close();
    file.Open();
    EntityRecord got;
    REQUIRE(file.Store().Get(EntityKey{0, 7}, &got).Ok());
    String<HeapAllocator> value(HeapAllocator::Default());
    REQUIRE(file.Store().GetMetadata("x", &value).Ok());
    CHECK(value.View() == StringView("y"));
    // Keys come back in key order across the high half too.
    REQUIRE(file.Store().Put(EntityRecord::Make(EntityKey{5, 1}, ByteSpan())).Ok());
    REQUIRE(file.Store().Put(EntityRecord::Make(EntityKey{0, ~u64(0)}, ByteSpan())).Ok());
    conformance::Collect all;
    REQUIRE(file.Store().Scan(KeyRange(), all).Ok());
    REQUIRE(all.keys.Size() == 3);
    CHECK(all.keys[0] == EntityKey{0, 7});
    CHECK(all.keys[1] == EntityKey{0, ~u64(0)});
    CHECK(all.keys[2] == EntityKey{5, 1});
}

TEST_CASE("SqlStore: another thread is Busy while a batch is open", "[storage][store][sql]")
{
    SqlMemoryFixture f;
    REQUIRE(f.Db().Put(conformance::Rec(1, 1)).Ok());
    REQUIRE(f.Db().Begin().Ok());
    Status read, write;
    Thread other(
        [&]
        {
            usize n = 0;
            read = f.Db().Count(&n);
            write = f.Db().Put(conformance::Rec(2, 2));
        });
    other.Join();
    CHECK(read.Is(ErrorCode::Busy));
    CHECK(write.Is(ErrorCode::Busy));
    REQUIRE(f.Db().Commit().Ok());
    usize n = 0;
    REQUIRE(f.Db().Count(&n).Ok());
    CHECK(n == 1);
}

namespace
{

u64 StateOf(Store& s)
{
    return conformance::Fingerprint(s);
}

// One batch of changes to a store holding Records(30).
void Batch(Store& s)
{
    REQUIRE(s.Begin().Ok());
    for (u64 k = 100; k < 140; ++k)
    {
        REQUIRE(s.Put(conformance::Rec(k, 5, 300)).Ok());
    }
    REQUIRE(s.Remove(EntityKey{0, 3}).Ok());
    REQUIRE(s.SetMetadata("m", "new").Ok());
    REQUIRE(s.Commit().Ok());
}

// Runs the batch under a fault plan on a fresh file, then looks at the file again. Returns the operations seen.
long long CrashRun(unsigned ops, long long fail_at, bool crash, u64 old_state, u64 new_state)
{
    database_test::TempDir dir;
    const io::Path path = dir / StringView("crash.db");
    database::SqliteDatabase db;
    database::OpenOptions options;
    options.location = path.View();
    options.sync_mode = database::SyncMode::Full;
    {
        UniquePtr<database::Connection> c;
        REQUIRE(db.Open(options, &c).Ok());
        UniquePtr<SqlStore> store;
        REQUIRE(SqlStore::Open(*c, &store).Ok());
        const Vector<EntityRecord> base = Records(30);
        REQUIRE(store->Begin().Ok());
        for (usize i = 0; i < base.Size(); ++i)
        {
            REQUIRE(store->Put(base[i]).Ok());
        }
        REQUIRE(store->SetMetadata("m", "old").Ok());
        REQUIRE(store->Commit().Ok());
    }
    database::sqlite_detail::SetFileFaults(ops, fail_at, crash);
    {
        UniquePtr<database::Connection> c;
        UniquePtr<SqlStore> store;
        if (db.Open(options, &c).Ok() && SqlStore::Open(*c, &store).Ok())
        {
            static_cast<void>(store->Begin());
            for (u64 k = 100; k < 140; ++k)
            {
                static_cast<void>(store->Put(conformance::Rec(k, 5, 300)));
            }
            static_cast<void>(store->Remove(EntityKey{0, 3}));
            static_cast<void>(store->SetMetadata("m", "new"));
            static_cast<void>(store->Commit());
        }
    }
    const long long seen = database::sqlite_detail::FileFaultOpsSeen();
    database::sqlite_detail::ClearFileFaults();
    UniquePtr<database::Connection> c;
    REQUIRE(db.Open(options, &c).Ok());
    UniquePtr<SqlStore> store;
    REQUIRE(SqlStore::Open(*c, &store).Ok());
    const u64 state = StateOf(*store);
    String<HeapAllocator> m(HeapAllocator::Default());
    REQUIRE(store->GetMetadata("m", &m).Ok());
    INFO("ops " << ops << " fail_at " << fail_at << " crash " << crash);
    CHECK((state == old_state || state == new_state));
    CHECK(((state == old_state) == (m.View() == StringView("old"))));  // the metadata moves with the records
    return seen;
}

}  // namespace

TEST_CASE("SqlStore: a failure at every write and sync of a batch leaves the old or the new state",
          "[storage][store][sql][crash]")
{
    u64 old_state = 0, new_state = 0;
    {
        MemoryStore reference;
        const Vector<EntityRecord> base = Records(30);
        for (usize i = 0; i < base.Size(); ++i)
        {
            REQUIRE(reference.Put(base[i]).Ok());
        }
        old_state = StateOf(reference);
        Batch(reference);
        new_state = StateOf(reference);
    }
    REQUIRE(old_state != new_state);
    for (const unsigned op : {database::sqlite_detail::kFaultWrite, database::sqlite_detail::kFaultSync})
    {
        const long long total = CrashRun(op, 1ll << 40, false, old_state, new_state);
        CHECK(total > 0);
        for (const bool crash : {false, true})
        {
            for (long long n = 0; n < total; ++n)
            {
                CrashRun(op, n, crash, old_state, new_state);
            }
        }
    }
}

TEST_CASE("SqlStore: a world saved to a SQL file loads back equal, and incremental saves reach the file",
          "[storage][store][sql]")
{
    ComponentRegistry registry;
    REQUIRE(RegisterBuiltinComponents(&registry).Ok());
    FileStore file;
    ecs::World world;
    KeyIndex index;
    KeyGenerator keys = KeyGenerator::Seeded(9);
    Vector<ecs::Entity> made;
    for (u32 i = 0; i < 300; ++i)
    {
        ecs::LocalTransform t;
        t.position = Vec3(static_cast<f32>(i), 1, 2);
        made.Push(world.Create(EntityKeyComponent{keys.Next()}, t));
    }
    WorldStorage storage(registry, file.Store());
    SaveSession session;
    SaveOptions options;
    options.mode = SaveMode::Incremental;
    options.session = &session;
    REQUIRE(storage.Save(world, index, options, nullptr).Ok());
    world.EndFrame();
    ecs::LocalTransform moved;
    moved.position = Vec3(-5, -5, -5);
    REQUIRE(world.Set(made[10], moved) == ecs::Status::Ok);
    REQUIRE(world.Destroy(made[20]) == ecs::Status::Ok);
    Report report;
    REQUIRE(storage.Save(world, index, options, &report).Ok());
    CHECK(report.reconciled == 0);
    CHECK(report.replaced == 1);
    CHECK(report.removed == 1);
    file.Close();
    file.Open();  // as after a restart
    WorldStorage again(registry, file.Store());
    ecs::World loaded;
    KeyIndex loaded_index;
    REQUIRE(again.Load(loaded, loaded_index, LoadOptions(), nullptr).Ok());
    test::RequireSameWorlds(registry, world, loaded);
    CHECK(loaded_index.Size() == 299);
}

}  // namespace vse::storage
