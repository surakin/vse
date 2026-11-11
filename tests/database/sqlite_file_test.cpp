// The SQLite backend on files (Phase 19 Day 254): the conformance suite on a database file, and what only a file database can be asked.

#include <catch2/catch_test_macros.hpp>

#include "conformance.h"
#include "database/sqlite/sqlite_database.h"
#include "io/file.h"
#include "temp_dir.h"

namespace engine_placeholder::database {

struct SqliteFileFixture {
    static constexpr bool kImplemented = true;
    static constexpr bool kSharedLocation = true;
    static const char* Name() { return "sqlite-file"; }
    SqliteFileFixture() : file_(dir_ / StringView("conformance.db")) {}
    Database& Db() { return db_; }
    OpenOptions Options() const {
        OpenOptions options;
        options.location = file_.View();
        return options;
    }

private:
    database_test::TempDir dir_;
    io::Path file_;
    SqliteDatabase db_;
};

DATABASE_CONFORMANCE_CASES(SqliteFileFixture)

namespace {

using conformance::Prep;
using conformance::Run;
using conformance::Scalar;
using conformance::StepRow;

OpenOptions At(const io::Path& path, OpenMode mode = OpenMode::Create) {
    OpenOptions options;
    options.location = path.View();
    options.mode = mode;
    return options;
}

TEST_CASE("SQLite file: create, close, reopen and read back", "[database][sqlite][file]") {
    database_test::TempDir dir;
    const io::Path path = dir / StringView("data.db");
    SqliteDatabase db;
    {
        UniquePtr<Connection> c;
        REQUIRE(db.Open(At(path), &c).Ok());
        Run(*c, "CREATE TABLE t (k INTEGER PRIMARY KEY, v TEXT)");
        Run(*c, "INSERT INTO t (k, v) VALUES (1, 'one'), (2, 'two')");
    }
    CHECK(io::IsFile(path));
    UniquePtr<Connection> c;
    REQUIRE(db.Open(At(path, OpenMode::ReadWrite), &c).Ok());
    CHECK(Scalar(*c, "SELECT count(*) FROM t") == 2);
    UniquePtr<Statement> st = Prep(*c, "SELECT v FROM t WHERE k = 2");
    REQUIRE(StepRow(*st));
    CHECK(st->Column(0).AsText() == StringView("two"));
}

TEST_CASE("SQLite file: a path with spaces and non-ASCII characters", "[database][sqlite][file]") {
    database_test::TempDir dir;
    const io::Path path = dir / StringView("sp ace \xC3\xBC\xE2\x9C\x93.db");
    SqliteDatabase db;
    {
        UniquePtr<Connection> c;
        REQUIRE(db.Open(At(path), &c).Ok());
        Run(*c, "CREATE TABLE t (k INTEGER PRIMARY KEY)");
    }
    UniquePtr<Connection> c;
    REQUIRE(db.Open(At(path, OpenMode::ReadWrite), &c).Ok());
    CHECK(Scalar(*c, "SELECT count(*) FROM t") == 0);
}

TEST_CASE("SQLite file: modes - a missing file, read-only", "[database][sqlite][file]") {
    database_test::TempDir dir;
    const io::Path path = dir / StringView("modes.db");
    SqliteDatabase db;
    UniquePtr<Connection> c;
    CHECK(db.Open(At(path, OpenMode::ReadWrite), &c).Is(ErrorCode::NotFound));
    CHECK(db.Open(At(path, OpenMode::ReadOnly), &c).Is(ErrorCode::NotFound));
    CHECK_FALSE(io::Exists(path)); // refusing to open created nothing
    {
        UniquePtr<Connection> writer;
        REQUIRE(db.Open(At(path), &writer).Ok());
        Run(*writer, "CREATE TABLE t (k INTEGER PRIMARY KEY)");
        Run(*writer, "INSERT INTO t (k) VALUES (1)");
    }
    REQUIRE(db.Open(At(path, OpenMode::ReadOnly), &c).Ok());
    CHECK(Scalar(*c, "SELECT count(*) FROM t") == 1);
    CHECK(c->Execute("INSERT INTO t (k) VALUES (2)").Is(ErrorCode::ReadOnly));
    CHECK(c->Supports(Feature::ConcurrentReaders)); // the file is in the write-ahead log, which a read-only connection reports as it finds it
}

TEST_CASE("SQLite file: a file that is not a database is Corrupt", "[database][sqlite][file]") {
    database_test::TempDir dir;
    const io::Path path = dir / StringView("junk.db");
    {
        io::FilePtr file;
        REQUIRE(io::File::Open(path, io::OpenMode::Write, io::kOpenNone, &file).Ok());
        char junk[8192];
        for (usize i = 0; i < sizeof(junk); ++i) {
            junk[i] = static_cast<char>('a' + i % 23);
        }
        REQUIRE(file->Write(junk, sizeof(junk)).Ok());
    }
    SqliteDatabase db;
    UniquePtr<Connection> c;
    CHECK(db.Open(At(path, OpenMode::ReadWrite), &c).Is(ErrorCode::Corrupt));
    CHECK(c == nullptr);
}

TEST_CASE("SQLite file: write-ahead log by default, rollback journals selectable", "[database][sqlite][file]") {
    database_test::TempDir dir;
    SqliteDatabase db;
    struct Case {
        JournalMode mode;
        const char* name;
        const char* file;
    };
    const Case cases[] = {{JournalMode::Default, "wal", "a.db"}, {JournalMode::WriteAhead, "wal", "b.db"}, {JournalMode::Delete, "delete", "c.db"}, {JournalMode::Truncate, "truncate", "d.db"}};
    for (const Case& test : cases) {
        const io::Path path = dir / StringView(test.file);
        OpenOptions options = At(path);
        options.journal_mode = test.mode;
        UniquePtr<Connection> c;
        REQUIRE(db.Open(options, &c).Ok());
        CHECK(c->Supports(Feature::WriteAheadLog));
        CHECK(c->Supports(Feature::ConcurrentReaders) == (test.mode == JournalMode::Default || test.mode == JournalMode::WriteAhead));
        UniquePtr<Statement> st = Prep(*c, "PRAGMA journal_mode");
        REQUIRE(StepRow(*st));
        CHECK(st->Column(0).AsText() == StringView(test.name));
    }
}

TEST_CASE("SQLite file: the clock and randomness come from the operating system", "[database][sqlite][file]") {
    SqliteDatabase db;
    UniquePtr<Connection> c;
    REQUIRE(db.Open(OpenOptions(), &c).Ok());
    UniquePtr<Statement> st = Prep(*c, "SELECT CAST(strftime('%Y', 'now') AS INTEGER), length(randomblob(16)), hex(randomblob(8)) <> hex(randomblob(8))");
    REQUIRE(StepRow(*st));
    CHECK(st->Column(0).AsInteger() >= 2026);
    CHECK(st->Column(1).AsInteger() == 16);
    CHECK(st->Column(2).AsInteger() == 1);
}

TEST_CASE("SQLite file: a big transaction spills past the page cache", "[database][sqlite][file]") {
    database_test::TempDir dir;
    SqliteDatabase db;
    UniquePtr<Connection> c;
    REQUIRE(db.Open(At(dir / StringView("big.db")), &c).Ok());
    Run(*c, "CREATE TABLE t (k INTEGER PRIMARY KEY, v BLOB)");
    {
        Transaction tx(*c);
        REQUIRE(tx.BeginStatus().Ok());
        UniquePtr<Statement> ins = Prep(*c, "INSERT INTO t (k, v) VALUES (?, zeroblob(2000))");
        for (i64 i = 1; i <= 3000; ++i) { // about 6 MB, more than the default 2 MB cache
            REQUIRE(ins->Reset().Ok());
            REQUIRE(ins->Bind(1, ValueView::Integer(i)).Ok());
            REQUIRE(conformance::StepToEnd(*ins).Ok());
        }
        // One statement that changes every row keeps a statement journal larger than the 64 KB SQLite holds in memory: it spills to a temporary
        // file, which the VFS creates (xOpen without a name) and removes when it is closed.
        Run(*c, "UPDATE t SET v = zeroblob(2100)");
        REQUIRE(tx.Commit().Ok());
    }
    CHECK(Scalar(*c, "SELECT count(*) FROM t") == 3000);
    CHECK(Scalar(*c, "SELECT min(length(v)) FROM t") == 2100);
    UniquePtr<Statement> check = Prep(*c, "PRAGMA integrity_check");
    REQUIRE(StepRow(*check));
    CHECK(check->Column(0).AsText() == StringView("ok"));
}

} // namespace
} // namespace engine_placeholder::database
