// The SQLite backend, complete (Phase 19 Day 255): settings, the statement cache, the integrity check, concurrent
// connections and a micro-benchmark.

#include <catch2/catch_test_macros.hpp>

#include "conformance.h"
#include "database/database_settings.h"
#include "database/sqlite/sqlite_database.h"
#include "database/sqlite/sqlite_os.h"
#include "io/file.h"
#include "os/os.h"
#include "os/console.h"
#include "settings/value.h"
#include "sync/atomic.h"
#include "sync/thread.h"
#include "temp_dir.h"

namespace engine_placeholder::database
{
namespace
{

using conformance::Prep;
using conformance::Run;
using conformance::Scalar;
using conformance::StepRow;

OpenOptions FileAt(const io::Path& path)
{
    OpenOptions options;
    options.location = path.View();
    return options;
}

// --- settings
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("Database settings: declared with their defaults and turned into options", "[database][settings]")
{
    settings::Registry registry;
    RegisterDatabaseSettings(&registry);
    RegisterDatabaseSettings(&registry);  // twice is harmless
    const OpenOptions options = OptionsFromSettings(registry, StringView("x.db"));
    CHECK(options.location == StringView("x.db"));
    CHECK(options.journal_mode == JournalMode::Default);
    CHECK(options.sync_mode == SyncMode::Default);
    CHECK(options.busy_timeout_ms == 5000);
    CHECK(options.integrity_check == IntegrityCheck::Off);
    CHECK(options.statement_cache == 32);
    CHECK(registry.Desc(registry.Find(StringView("database.busy_timeout_ms")))->group[0] == 'd');
}

TEST_CASE("Database settings: a published value reaches the next Open", "[database][settings]")
{
    settings::Registry registry;
    RegisterDatabaseSettings(&registry);
    registry.Publish(registry.Find(StringView("database.journal_mode")),
                     settings::Value::FromEnum(static_cast<u32>(JournalMode::Delete)));
    registry.Publish(registry.Find(StringView("database.synchronous")),
                     settings::Value::FromEnum(static_cast<u32>(SyncMode::Full)));
    registry.Publish(registry.Find(StringView("database.busy_timeout_ms")), settings::Value::FromInt(25));
    registry.Publish(registry.Find(StringView("database.integrity_check")),
                     settings::Value::FromEnum(static_cast<u32>(IntegrityCheck::Full)));
    registry.Publish(registry.Find(StringView("database.statement_cache")), settings::Value::FromInt(0));
    database_test::TempDir dir;
    const io::Path path = dir / StringView("s.db");
    const OpenOptions options = OptionsFromSettings(registry, path.View());
    CHECK(options.journal_mode == JournalMode::Delete);
    CHECK(options.sync_mode == SyncMode::Full);
    CHECK(options.busy_timeout_ms == 25);
    CHECK(options.integrity_check == IntegrityCheck::Full);
    CHECK(options.statement_cache == 0);
    SqliteDatabase db;
    UniquePtr<Connection> c;
    REQUIRE(db.Open(options, &c).Ok());
    CHECK_FALSE(c->Supports(Feature::ConcurrentReaders));  // a rollback journal, as set
    Run(*c, "CREATE TABLE t (a INTEGER)");
    CHECK(Scalar(*c, "SELECT count(*) FROM t") == 0);
}

// --- the statement cache
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("Statement cache: the same text is compiled once, bindings do not leak", "[database][sqlite][cache]")
{
    SqliteDatabase db;
    UniquePtr<Connection> c;
    REQUIRE(db.Open(OpenOptions(), &c).Ok());
    Run(*c, "CREATE TABLE t (k INTEGER PRIMARY KEY, v TEXT)");
    {
        UniquePtr<Statement> insert = Prep(*c, "INSERT INTO t (k, v) VALUES (?, ?)");
        REQUIRE(insert->Bind(1, ValueView::Integer(1)).Ok());
        REQUIRE(insert->Bind(2, ValueView::Text(StringView("a"))).Ok());
        bool row = true;
        REQUIRE(insert->Step(&row).Ok());
    }
    const unsigned long long before = sqlite_detail::PreparedStatementCount();
    for (i64 k = 2; k <= 50; ++k)
    {
        UniquePtr<Statement> insert = Prep(*c, "INSERT INTO t (k, v) VALUES (?, ?)");
        bool row = true;
        // A reused statement starts with no bindings: the second parameter is NULL until bound.
        REQUIRE(insert->Bind(1, ValueView::Integer(k)).Ok());
        REQUIRE(insert->Step(&row).Ok());
    }
    CHECK(sqlite_detail::PreparedStatementCount() == before);
    CHECK(Scalar(*c, "SELECT count(*) FROM t WHERE v IS NULL") == 49);
}

TEST_CASE("Statement cache: size 0 compiles every time, a statement used again after a schema change still works",
          "[database][sqlite][cache]")
{
    SqliteDatabase db;
    OpenOptions options;
    options.statement_cache = 0;
    UniquePtr<Connection> c;
    REQUIRE(db.Open(options, &c).Ok());
    Run(*c, "CREATE TABLE t (a INTEGER)");
    const unsigned long long before = sqlite_detail::PreparedStatementCount();
    for (int i = 0; i < 5; ++i)
    {
        CHECK(Scalar(*c, "SELECT count(*) FROM t") == 0);
    }
    CHECK(sqlite_detail::PreparedStatementCount() == before + 5);

    UniquePtr<Connection> cached;
    REQUIRE(db.Open(OpenOptions(), &cached).Ok());
    Run(*cached, "CREATE TABLE u (a INTEGER)");
    CHECK(Scalar(*cached, "SELECT count(*) FROM u") == 0);  // cached now
    Run(*cached, "ALTER TABLE u ADD COLUMN b INTEGER");
    Run(*cached, "INSERT INTO u (a, b) VALUES (1, 2)");
    CHECK(Scalar(*cached, "SELECT count(*) FROM u") == 1);  // the cached statement is recompiled by SQLite, not stale
}

TEST_CASE("Statement cache: a finished read releases its lock so a writer can proceed", "[database][sqlite][cache]")
{
    database_test::TempDir dir;
    const io::Path path = dir / StringView("c.db");
    SqliteDatabase db;
    UniquePtr<Connection> a;
    UniquePtr<Connection> b;
    REQUIRE(db.Open(FileAt(path), &a).Ok());
    REQUIRE(db.Open(FileAt(path), &b).Ok());
    Run(*a, "CREATE TABLE t (k INTEGER)");
    Run(*a, "INSERT INTO t VALUES (1)");
    {
        UniquePtr<Statement> read = Prep(*a, "SELECT k FROM t");
        REQUIRE(StepRow(*read));  // stopped on a row, not finished
    }  // returned to the cache: reset, so the read lock is gone
    Run(*b, "PRAGMA wal_checkpoint(TRUNCATE)");
    Run(*b, "INSERT INTO t VALUES (2)");
    CHECK(Scalar(*a, "SELECT count(*) FROM t") == 2);
}

TEST_CASE("Sync mode: the default is NORMAL with the write-ahead log and FULL with a rollback journal",
          "[database][sqlite]")
{
    database_test::TempDir dir;
    SqliteDatabase db;
    struct Case
    {
        JournalMode journal;
        SyncMode sync;
        i64 expected;  // PRAGMA synchronous: 1 NORMAL, 2 FULL
    };
    const Case cases[] = {{JournalMode::WriteAhead, SyncMode::Default, 1},
                          {JournalMode::Delete, SyncMode::Default, 2},
                          {JournalMode::Truncate, SyncMode::Default, 2},
                          {JournalMode::Delete, SyncMode::Normal, 1}};
    usize n = 0;
    for (const Case& c : cases)
    {
        const io::Path path = dir / StringView(n == 0 ? "s0.db" : n == 1 ? "s1.db" : n == 2 ? "s2.db" : "s3.db");
        ++n;
        OpenOptions options = FileAt(path);
        options.journal_mode = c.journal;
        options.sync_mode = c.sync;
        UniquePtr<Connection> connection;
        REQUIRE(db.Open(options, &connection).Ok());
        CHECK(Scalar(*connection, "PRAGMA synchronous") == c.expected);
    }
}

// --- the integrity check
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("Integrity check: a sound file opens, a damaged one is Corrupt", "[database][sqlite][integrity]")
{
    database_test::TempDir dir;
    const io::Path path = dir / StringView("i.db");
    SqliteDatabase db;
    {
        UniquePtr<Connection> c;
        REQUIRE(db.Open(FileAt(path), &c).Ok());
        Run(*c, "CREATE TABLE t (k INTEGER PRIMARY KEY, v TEXT)");
        Run(*c, "CREATE INDEX t_v ON t (v)");
        Run(*c, "INSERT INTO t (v) VALUES ('a'), ('b'), ('c')");
    }
    for (const IntegrityCheck check : {IntegrityCheck::Quick, IntegrityCheck::Full})
    {
        OpenOptions options = FileAt(path);
        options.integrity_check = check;
        UniquePtr<Connection> c;
        REQUIRE(db.Open(options, &c).Ok());
        CHECK(Scalar(*c, "SELECT count(*) FROM t") == 3);
    }
    // A file that is not a database fails the check (or the open) as Corrupt.
    const io::Path junk = dir / StringView("junk.db");
    {
        io::FilePtr file;
        REQUIRE(io::File::Open(junk, io::OpenMode::Write, io::kOpenNone, &file).Ok());
        char text[8192];
        for (usize i = 0; i < sizeof(text); ++i)
        {
            text[i] = static_cast<char>('a' + i % 23);
        }
        REQUIRE(file->Write(text, sizeof(text)).Ok());
    }
    OpenOptions options = FileAt(junk);
    options.integrity_check = IntegrityCheck::Quick;
    UniquePtr<Connection> c;
    CHECK(db.Open(options, &c).Is(ErrorCode::Corrupt));
}

// --- concurrency
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("Concurrency: a second writer gets Busy while the first holds the write lock",
          "[database][sqlite][concurrency]")
{
    database_test::TempDir dir;
    const io::Path path = dir / StringView("w.db");
    SqliteDatabase db;
    OpenOptions options = FileAt(path);
    options.busy_timeout_ms = 0;
    UniquePtr<Connection> a;
    UniquePtr<Connection> b;
    REQUIRE(db.Open(options, &a).Ok());
    REQUIRE(db.Open(options, &b).Ok());
    Run(*a, "CREATE TABLE t (k INTEGER)");
    REQUIRE(a->Begin(TransactionKind::Immediate).Ok());
    Run(*a, "INSERT INTO t VALUES (1)");
    CHECK(b->Begin(TransactionKind::Immediate).Is(ErrorCode::Busy));
    CHECK_FALSE(b->InTransaction());
    // A reader is not blocked by the writer, and sees the state before the writer's commit.
    CHECK(Scalar(*b, "SELECT count(*) FROM t") == 0);
    REQUIRE(a->Commit().Ok());
    CHECK(Scalar(*b, "SELECT count(*) FROM t") == 1);
    REQUIRE(b->Begin(TransactionKind::Immediate).Ok());
    REQUIRE(b->Rollback().Ok());
}

TEST_CASE("Concurrency: savepoints inside a write transaction keep the write lock and hide nothing from readers",
          "[database][sqlite][concurrency]")
{
    database_test::TempDir dir;
    const io::Path path = dir / StringView("sp.db");
    SqliteDatabase db;
    OpenOptions options = FileAt(path);
    options.busy_timeout_ms = 0;
    UniquePtr<Connection> a;
    UniquePtr<Connection> b;
    REQUIRE(db.Open(options, &a).Ok());
    REQUIRE(db.Open(options, &b).Ok());
    Run(*a, "CREATE TABLE t (k INTEGER)");
    Run(*a, "INSERT INTO t VALUES (1)");
    REQUIRE(a->Begin(TransactionKind::Immediate).Ok());
    REQUIRE(a->Savepoint("inner").Ok());
    Run(*a, "INSERT INTO t VALUES (2)");
    REQUIRE(a->RollbackToSavepoint("inner").Ok());
    REQUIRE(a->ReleaseSavepoint("inner").Ok());
    CHECK(
        b->Begin(TransactionKind::Immediate).Is(ErrorCode::Busy));  // the outer transaction still holds the write lock
    CHECK(Scalar(*b, "SELECT count(*) FROM t") == 1);
    Run(*a, "INSERT INTO t VALUES (3)");
    REQUIRE(a->Commit().Ok());
    CHECK(Scalar(*b, "SELECT count(*) FROM t") == 2);  // the rolled-back row never appeared; the later one did
}

TEST_CASE("Concurrency: one writer thread and several reader threads", "[database][sqlite][concurrency]")
{
    database_test::TempDir dir;
    const io::Path path = dir / StringView("m.db");
    SqliteDatabase db;
    {
        UniquePtr<Connection> c;
        REQUIRE(db.Open(FileAt(path), &c).Ok());
        Run(*c, "CREATE TABLE t (k INTEGER PRIMARY KEY)");
        Run(*c, "INSERT INTO t (k) VALUES (0)");
    }
    constexpr int kWrites = 200;
    Atomic<int> failures{0};
    Atomic<bool> stop{false};

    auto writer = [&]()
    {
        UniquePtr<Connection> c;
        if (db.Open(FileAt(path), &c).Failed())
        {
            failures.FetchAdd(1);
            return;
        }
        for (int k = 1; k <= kWrites; ++k)
        {
            if (c->Begin(TransactionKind::Immediate).Failed())
            {
                failures.FetchAdd(1);
                continue;
            }
            UniquePtr<Statement> insert;
            bool row = true;
            if (c->Prepare("INSERT INTO t (k) VALUES (?)", &insert).Failed() ||
                insert->Bind(1, ValueView::Integer(k)).Failed() || insert->Step(&row).Failed() || c->Commit().Failed())
            {
                failures.FetchAdd(1);
                static_cast<void>(c->Rollback());  // already counted as a failure
            }
        }
        stop.Store(true);
    };
    auto reader = [&]()
    {
        UniquePtr<Connection> c;
        if (db.Open(FileAt(path), &c).Failed())
        {
            failures.FetchAdd(1);
            return;
        }
        i64 last = 0;
        while (!stop.Load())
        {
            UniquePtr<Statement> st;
            bool row = false;
            if (c->Prepare("SELECT max(k), count(*) FROM t", &st).Failed() || st->Step(&row).Failed() || !row)
            {
                failures.FetchAdd(1);
                return;
            }
            const i64 max = st->Column(0).AsInteger();
            const i64 count = st->Column(1).AsInteger();
            if (count != max + 1 || max < last)
            {  // every committed state is whole; time only moves forward
                failures.FetchAdd(1);
            }
            last = max;
        }
    };
    Thread w(writer);
    Thread r0(reader);
    Thread r1(reader);
    Thread r2(reader);
    w.Join();
    r0.Join();
    r1.Join();
    r2.Join();
    CHECK(failures.Load() == 0);
    UniquePtr<Connection> c;
    REQUIRE(db.Open(FileAt(path), &c).Ok());
    CHECK(Scalar(*c, "SELECT count(*) FROM t") == kWrites + 1);
}

TEST_CASE("Concurrency: writers on separate connections queue behind the busy timeout",
          "[database][sqlite][concurrency]")
{
    database_test::TempDir dir;
    const io::Path path = dir / StringView("q.db");
    SqliteDatabase db;
    {
        UniquePtr<Connection> c;
        REQUIRE(db.Open(FileAt(path), &c).Ok());
        Run(*c, "CREATE TABLE t (k INTEGER PRIMARY KEY AUTOINCREMENT, who INTEGER)");
    }
    constexpr int kThreads = 4;
    constexpr int kEach = 50;
    Atomic<int> failures{0};
    auto work = [&](int who)
    {
        OpenOptions options = FileAt(path);
        options.busy_timeout_ms = 30000;
        UniquePtr<Connection> c;
        if (db.Open(options, &c).Failed())
        {
            failures.FetchAdd(1);
            return;
        }
        for (int i = 0; i < kEach; ++i)
        {
            UniquePtr<Statement> st;
            bool row = true;
            if (c->Begin(TransactionKind::Immediate).Failed() ||
                c->Prepare("INSERT INTO t (who) VALUES (?)", &st).Failed() ||
                st->Bind(1, ValueView::Integer(who)).Failed() || st->Step(&row).Failed() || c->Commit().Failed())
            {
                failures.FetchAdd(1);
                if (c->InTransaction())
                {
                    static_cast<void>(c->Rollback());  // already counted as a failure
                }
            }
        }
    };
    Thread t0(work, 0);
    Thread t1(work, 1);
    Thread t2(work, 2);
    Thread t3(work, 3);
    t0.Join();
    t1.Join();
    t2.Join();
    t3.Join();
    CHECK(failures.Load() == 0);
    UniquePtr<Connection> c;
    REQUIRE(db.Open(FileAt(path), &c).Ok());
    CHECK(Scalar(*c, "SELECT count(*) FROM t") == kThreads * kEach);
}

// --- the micro-benchmark
// ---------------------------------------------------------------------------------------------------------------------

// Hidden: `database_tests "[database-bench]"` prints rows per second for inserts (one transaction), point reads and the
// same reads with the cache off.
TEST_CASE("Database benchmark", "[.][database-bench]")
{
    database_test::TempDir dir;
    const io::Path path = dir / StringView("bench.db");
    SqliteDatabase db;
    UniquePtr<Connection> c;
    REQUIRE(db.Open(FileAt(path), &c).Ok());
    Run(*c, "CREATE TABLE t (k INTEGER PRIMARY KEY, v TEXT)");
    constexpr int kRows = 20000;
    const u64 t0 = os::MonotonicTimeNanos();
    REQUIRE(c->Begin(TransactionKind::Immediate).Ok());
    for (int k = 0; k < kRows; ++k)
    {
        UniquePtr<Statement> st = Prep(*c, "INSERT INTO t (k, v) VALUES (?, 'value')");
        REQUIRE(st->Bind(1, ValueView::Integer(k)).Ok());
        bool row = true;
        REQUIRE(st->Step(&row).Ok());
    }
    REQUIRE(c->Commit().Ok());
    const u64 t1 = os::MonotonicTimeNanos();
    auto reads = [&](Connection& conn)
    {
        for (int k = 0; k < kRows; ++k)
        {
            UniquePtr<Statement> st = Prep(conn, "SELECT v FROM t WHERE k = ?");
            REQUIRE(st->Bind(1, ValueView::Integer(k)).Ok());
            REQUIRE(StepRow(*st));
        }
    };
    reads(*c);
    const u64 t2 = os::MonotonicTimeNanos();
    OpenOptions nocache = FileAt(path);
    nocache.statement_cache = 0;
    UniquePtr<Connection> plain;
    REQUIRE(db.Open(nocache, &plain).Ok());
    reads(*plain);
    const u64 t3 = os::MonotonicTimeNanos();
    auto per_second = [&](u64 ns) { return static_cast<double>(kRows) * 1e9 / static_cast<double>(ns); };
    os::Print("inserts %.0f/s, cached point reads %.0f/s, uncached point reads %.0f/s\n", per_second(t1 - t0),
              per_second(t2 - t1), per_second(t3 - t2));
}

}  // namespace
}  // namespace engine_placeholder::database
