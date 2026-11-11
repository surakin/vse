// Crash safety of the SQLite backend (Phase 19 Day 254): a failure injected into the Nth write, sync or truncation of a
// transaction, or a "crash" at that point after which nothing more reaches the disk, must leave the old rows or all the
// new rows, never a mix, and a database that passes its own check.

#include <catch2/catch_test_macros.hpp>

#include "conformance.h"
#include "database/sqlite/sqlite_database.h"
#include "database/sqlite/sqlite_os.h"
#include "temp_dir.h"

namespace engine_placeholder::database
{
namespace
{

using conformance::Prep;
using conformance::Run;
using conformance::Scalar;
using conformance::StepRow;

constexpr i64 kOldRows = 100;
constexpr i64 kNewRows = 200;

OpenOptions Options(const io::Path& path, JournalMode mode)
{
    OpenOptions options;
    options.location = path.View();
    options.journal_mode = mode;
    options.sync_mode = SyncMode::Full;
    return options;
}

// Adds kNewRows rows in one transaction; whatever fails is ignored: the test looks at the file afterwards.
void WriteTransaction(Database& db, const OpenOptions& options)
{
    UniquePtr<Connection> c;
    if (db.Open(options, &c).Failed())
    {
        return;
    }
    Transaction tx(*c);
    if (tx.BeginStatus().Failed())
    {
        return;
    }
    UniquePtr<Statement> ins;
    if (c->Prepare("INSERT INTO t (k, v) VALUES (?, zeroblob(100))", &ins).Failed())
    {
        return;
    }
    for (i64 i = 1; i <= kNewRows; ++i)
    {
        static_cast<void>(ins->Reset());
        if (ins->Bind(1, ValueView::Integer(kOldRows + i)).Failed())
        {
            return;
        }
        bool row = false;
        if (ins->Step(&row).Failed())
        {
            return;
        }
    }
    static_cast<void>(tx.Commit());  // under a fault plan the commit may fail; the test looks at the file afterwards
}

// One run: a database of kOldRows rows, a transaction under the fault plan, then a fresh look at the file.
// Returns the number of faultable operations the transaction made (meaningful for a plan that never fires).
long long RunScenario(JournalMode mode, unsigned ops, long long fail_at, bool crash)
{
    database_test::TempDir dir;
    const io::Path path = dir / StringView("crash.db");
    SqliteDatabase db;
    {
        UniquePtr<Connection> c;
        REQUIRE(db.Open(Options(path, mode), &c).Ok());
        Run(*c, "CREATE TABLE t (k INTEGER PRIMARY KEY, v BLOB)");
        Transaction tx(*c);
        REQUIRE(tx.BeginStatus().Ok());
        UniquePtr<Statement> ins = Prep(*c, "INSERT INTO t (k, v) VALUES (?, zeroblob(100))");
        for (i64 i = 1; i <= kOldRows; ++i)
        {
            REQUIRE(ins->Reset().Ok());
            REQUIRE(ins->Bind(1, ValueView::Integer(i)).Ok());
            REQUIRE(conformance::StepToEnd(*ins).Ok());
        }
        REQUIRE(tx.Commit().Ok());
    }
    sqlite_detail::SetFileFaults(ops, fail_at, crash);
    WriteTransaction(db, Options(path, mode));
    const long long seen = sqlite_detail::FileFaultOpsSeen();
    sqlite_detail::ClearFileFaults();

    UniquePtr<Connection> c;
    REQUIRE(db.Open(Options(path, mode), &c).Ok());
    UniquePtr<Statement> check = Prep(*c, "PRAGMA integrity_check");
    REQUIRE(StepRow(*check));
    REQUIRE(check->Column(0).AsText() == StringView("ok"));
    const i64 rows = Scalar(*c, "SELECT count(*) FROM t");
    INFO("mode " << static_cast<int>(mode) << " ops " << ops << " fail_at " << fail_at << " crash " << crash << " rows "
                 << rows);
    CHECK((rows == kOldRows || rows == kOldRows + kNewRows));
    return seen;
}

TEST_CASE("SQLite crash safety: a failure at every write, sync and truncation leaves the old or the new rows",
          "[database][sqlite][crash]")
{
    const JournalMode modes[] = {JournalMode::Delete, JournalMode::Truncate, JournalMode::WriteAhead};
    const unsigned ops[] = {sqlite_detail::kFaultWrite, sqlite_detail::kFaultSync, sqlite_detail::kFaultTruncate};
    for (const JournalMode mode : modes)
    {
        for (const unsigned op : ops)
        {
            const long long total = RunScenario(mode, op, 1ll << 40, false);  // never fires: counts the operations
            if (op != sqlite_detail::kFaultTruncate)
            {
                CHECK(total >
                      0);  // a transaction of 200 rows does write and sync (a truncation only in some journal modes)
            }
            for (const bool crash : {false, true})
            {
                for (long long n = 0; n < total; ++n)
                {
                    RunScenario(mode, op, n, crash);
                }
            }
            CHECK(RunScenario(mode, op, 1ll << 40, false) == total);  // the count is stable
        }
    }
}

TEST_CASE("SQLite crash safety: a transaction that was committed before the crash survives it",
          "[database][sqlite][crash]")
{
    database_test::TempDir dir;
    const io::Path path = dir / StringView("committed.db");
    SqliteDatabase db;
    for (const JournalMode mode : {JournalMode::Delete, JournalMode::WriteAhead})
    {
        UniquePtr<Connection> c;
        REQUIRE(db.Open(Options(path, mode), &c).Ok());
        Run(*c, "CREATE TABLE IF NOT EXISTS u (k INTEGER PRIMARY KEY)");
        Run(*c, "INSERT OR REPLACE INTO u (k) VALUES (1)");
        sqlite_detail::SetFileFaults(sqlite_detail::kFaultWrite | sqlite_detail::kFaultSync |
                                         sqlite_detail::kFaultTruncate,
                                     0, true);  // crash at once
        CHECK(c->Execute("INSERT OR REPLACE INTO u (k) VALUES (2)").Failed());
        c.Reset();
        sqlite_detail::ClearFileFaults();
        REQUIRE(db.Open(Options(path, mode), &c).Ok());
        CHECK(Scalar(*c, "SELECT count(*) FROM u WHERE k = 1") == 1);
    }
}

}  // namespace
}  // namespace engine_placeholder::database
