// The SQLite backend on private in-memory databases (Phase 19 Day 253): the conformance suite, and what only this
// backend can be asked.

#include <catch2/catch_test_macros.hpp>

#include "conformance.h"
#include "database/sqlite/sqlite_database.h"

namespace engine_placeholder::database
{

struct SqliteMemoryFixture
{
    static constexpr bool kImplemented = true;
    static constexpr bool kSharedLocation = false;  // every in-memory database belongs to the connection that opened it
    static const char* Name() { return "sqlite-memory"; }
    Database& Db() { return db_; }
    OpenOptions Options() const { return OpenOptions(); }

private:
    SqliteDatabase db_;
};

DATABASE_CONFORMANCE_CASES(SqliteMemoryFixture)

namespace
{

TEST_CASE("SQLite backend: identity and features", "[database][sqlite]")
{
    SqliteDatabase db;
    CHECK(StringView(db.Name()) == StringView("sqlite"));
    CHECK(db.Supports(Feature::Savepoints));
    CHECK(db.Supports(Feature::ForeignKeys));
    CHECK(db.Supports(Feature::WriteAheadLog));  // for a file; an in-memory connection reports its own answer
    UniquePtr<Connection> memory;
    REQUIRE(db.Open(OpenOptions(), &memory).Ok());
    CHECK_FALSE(memory->Supports(Feature::WriteAheadLog));
    CHECK_FALSE(memory->Supports(Feature::ConcurrentReaders));
}

TEST_CASE("SQLite backend: an in-memory database is never read-only and has no write-ahead log", "[database][sqlite]")
{
    SqliteDatabase db;
    UniquePtr<Connection> conn;
    OpenOptions read_only;
    read_only.mode = OpenMode::ReadOnly;
    CHECK(db.Open(read_only, &conn).Is(ErrorCode::Unsupported));
    OpenOptions wal;
    wal.journal_mode = JournalMode::WriteAhead;
    CHECK(db.Open(wal, &conn).Is(ErrorCode::Unsupported));
    CHECK(conn == nullptr);
    OpenOptions named;
    named.location = StringView(":memory:");
    CHECK(db.Open(named, &conn).Ok());
}

TEST_CASE("SQLite backend: two in-memory connections are two databases", "[database][sqlite]")
{
    SqliteDatabase db;
    UniquePtr<Connection> a;
    UniquePtr<Connection> b;
    REQUIRE(db.Open(OpenOptions(), &a).Ok());
    REQUIRE(db.Open(OpenOptions(), &b).Ok());
    REQUIRE(a->Execute("CREATE TABLE t (k INTEGER PRIMARY KEY)").Ok());
    CHECK(b->Execute("INSERT INTO t (k) VALUES (1)").Is(ErrorCode::NotFound));
}

TEST_CASE("SQLite backend: a statement outlives its connection and then fails with Misuse", "[database][sqlite]")
{
    SqliteDatabase db;
    UniquePtr<Connection> conn;
    REQUIRE(db.Open(OpenOptions(), &conn).Ok());
    REQUIRE(conn->Execute("CREATE TABLE t (k INTEGER PRIMARY KEY)").Ok());
    UniquePtr<Statement> st;
    REQUIRE(conn->Prepare("SELECT k FROM t", &st).Ok());
    conn.Reset();
    bool row = false;
    CHECK(st->Step(&row).Is(ErrorCode::Misuse));
    CHECK(st->Bind(1, ValueView::Integer(1)).Is(ErrorCode::Misuse));
    CHECK(st->Reset().Is(ErrorCode::Misuse));
}

TEST_CASE("SQLite backend: result codes map to portable codes, and the message is kept", "[database][sqlite]")
{
    SqliteDatabase db;
    UniquePtr<Connection> c;
    REQUIRE(db.Open(OpenOptions(), &c).Ok());
    REQUIRE(c->Execute("CREATE TABLE t (k INTEGER PRIMARY KEY, v TEXT NOT NULL)").Ok());
    const Status constraint = c->Execute("INSERT INTO t (k, v) VALUES (1, NULL)");
    CHECK(constraint.Is(ErrorCode::Constraint));
    CHECK(constraint.BackendCode() == 1299);  // SQLITE_CONSTRAINT_NOTNULL, the extended code
    CHECK(constraint.Message().Length() > 0);
    UniquePtr<Statement> st;
    CHECK(c->Prepare("SELECT missing_column FROM t", &st).Is(ErrorCode::NotFound));
    CHECK(c->Prepare("SELECT k FROM t; SELECT k FROM t", &st).Is(ErrorCode::Misuse));
    CHECK(c->Prepare("   ", &st).Is(ErrorCode::Syntax));
    CHECK(c->Prepare("SELECT 1; ", &st).Ok());               // a trailing semicolon is not a second statement
    CHECK(c->Prepare("SELECT 1 -- a comment\n", &st).Ok());  // nor is a trailing comment
    CHECK(c->Prepare("SELECT 1; /* c */ ;", &st).Ok());
    CHECK(c->Prepare("SELECT 1; SELEKT", &st)
              .Is(ErrorCode::Misuse));  // a bad second statement is still a second statement
    CHECK(c->Prepare(SqlText::Unchecked(StringView()), &st).Is(ErrorCode::Syntax));
}

TEST_CASE("SQLite backend: NaN is refused instead of becoming NULL", "[database][sqlite]")
{
    SqliteDatabase db;
    UniquePtr<Connection> c;
    REQUIRE(db.Open(OpenOptions(), &c).Ok());
    UniquePtr<Statement> st;
    REQUIRE(c->Prepare("SELECT ?", &st).Ok());
    CHECK(st->Bind(1, ValueView::Real(Limits<f64>::QuietNaN())).Is(ErrorCode::Misuse));
}

TEST_CASE("SQLite backend: 1,000 statements prepared and destroyed", "[database][sqlite]")
{
    SqliteDatabase db;
    UniquePtr<Connection> c;
    REQUIRE(db.Open(OpenOptions(), &c).Ok());
    for (int i = 0; i < 1000; ++i)
    {
        UniquePtr<Statement> st;
        REQUIRE(c->Prepare("SELECT 1", &st).Ok());
        bool row = false;
        REQUIRE(st->Step(&row).Ok());
        REQUIRE(row);
    }
}

TEST_CASE("SQLite backend: savepoints need an identifier and the connection closes with a transaction open",
          "[database][sqlite]")
{
    SqliteDatabase db;
    UniquePtr<Connection> c;
    REQUIRE(db.Open(OpenOptions(), &c).Ok());
    REQUIRE(c->Begin(TransactionKind::Deferred).Ok());
    CHECK(c->Savepoint("release").Ok());  // a keyword is fine: the name is quoted
    CHECK(c->Savepoint("x\"; DROP TABLE t; --").Is(ErrorCode::Misuse));
    CHECK(c->Savepoint("").Is(ErrorCode::Misuse));
    CHECK(c->Savepoint("1abc").Is(ErrorCode::Misuse));
    c.Reset();  // rolls the open transaction back; asan checks nothing leaks
}

}  // namespace
}  // namespace engine_placeholder::database
