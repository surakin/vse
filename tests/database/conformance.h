#pragma once

// The database conformance suite (Phase 19 Day 252): what every backend of the database interface must do. A backend's
// test file includes this header and instantiates the suite once with a fixture:
//
//     struct MyBackendFixture {
//         static constexpr bool kImplemented = true;      // false makes every case skip (a backend that does not exist
//         yet) static constexpr bool kSharedLocation = true;   // false: a second Open never reaches the first
//         connection's database (a private in-memory
//                                                          // database), so the cases that need two connections to one
//                                                          database skip
//         static const char* Name();                       // "memory", "sqlite"
//         MyBackendFixture();                              // picks a fresh, unique database location (and removes it
//         in the destructor) database::Database& Db();                        // the backend database::OpenOptions
//         Options() const;           // opens the fixture's one shared database (same location every call)
//     };
//     DATABASE_CONFORMANCE_CASES(MyBackendFixture)
//
// Every case uses only the portable statement subset of docs/superpowers/specs/2026-11-11-storage-design.md (section 3,
// spike c), so the in-memory database runs it too. A case about an optional feature checks `Supports` first and, when
// the feature is absent, that using it fails as documented.

#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "database/database.h"
#include "lang/limits.h"

namespace engine_placeholder::database::conformance
{

// --- helpers
// ---------------------------------------------------------------------------------------------------------------------

template <class F> struct Session
{
    F fixture;
    UniquePtr<Connection> conn;

    Session()
    {
        if (!F::kImplemented)
        {
            SKIP("the backend " << F::Name() << " is not implemented yet");
        }
        const Status status = fixture.Db().Open(fixture.Options(), &conn);
        REQUIRE(status.Ok());
        REQUIRE(conn != nullptr);
    }
    // Another connection to the same database.
    UniquePtr<Connection> Another()
    {
        UniquePtr<Connection> other;
        REQUIRE(fixture.Db().Open(fixture.Options(), &other).Ok());
        return other;
    }
};

inline void Run(Connection& c, SqlText sql)
{
    const Status s = c.Execute(sql);
    INFO("execute failed: " << ErrorCodeName(s.Code()));
    REQUIRE(s.Ok());
}

inline UniquePtr<Statement> Prep(Connection& c, SqlText sql)
{
    UniquePtr<Statement> st;
    const Status s = c.Prepare(sql, &st);
    INFO("prepare failed: " << ErrorCodeName(s.Code()));
    REQUIRE(s.Ok());
    REQUIRE(st != nullptr);
    return st;
}

inline bool StepRow(Statement& st)
{
    bool row = false;
    REQUIRE(st.Step(&row).Ok());
    return row;
}

// Runs a statement that returns one integer in one row and returns it.
inline i64 Scalar(Connection& c, SqlText sql)
{
    UniquePtr<Statement> st = Prep(c, sql);
    REQUIRE(StepRow(*st));
    REQUIRE(st->Column(0).Type() == ValueType::Integer);
    return st->Column(0).AsInteger();
}

inline Status StepToEnd(Statement& st)
{
    bool row = true;
    while (row)
    {
        const Status s = st.Step(&row);
        if (s.Failed())
        {
            return s;
        }
    }
    return Status::Success();
}

// Inserts one value into the table `kv(k INTEGER PRIMARY KEY, v <type>)` and reads it back.
inline ValueView Echo(Statement& insert, Statement& select, i64 key, const ValueView& value)
{
    REQUIRE(insert.Reset().Ok());
    REQUIRE(insert.Bind(1, ValueView::Integer(key)).Ok());
    REQUIRE(insert.Bind(2, value).Ok());
    REQUIRE(StepToEnd(insert).Ok());
    REQUIRE(select.Reset().Ok());
    REQUIRE(select.Bind(1, ValueView::Integer(key)).Ok());
    REQUIRE(StepRow(select));
    return select.Column(0);
}

// --- value round trips
// ---------------------------------------------------------------------------------------------------------------------

template <class F> void OpenAndClose()
{
    Session<F> s;
    i64 version = -1;
    REQUIRE(s.conn->SchemaVersion(&version).Ok());
    CHECK(version == 0);
    CHECK_FALSE(s.conn->InTransaction());
    s.conn.Reset();
    UniquePtr<Connection> again;
    CHECK(s.fixture.Db().Open(s.fixture.Options(), &again).Ok());
}

template <class F> void IntegerRoundTrip()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v INTEGER)");
    UniquePtr<Statement> ins = Prep(*s.conn, "INSERT INTO kv (k, v) VALUES (?, ?)");
    UniquePtr<Statement> sel = Prep(*s.conn, "SELECT v FROM kv WHERE k = ?");
    const i64 values[] = {0, 1, -1, 255, -256, 4294967296LL, Limits<i64>::Max(), Limits<i64>::Min()};
    i64 key = 1;
    for (const i64 v : values)
    {
        const ValueView got = Echo(*ins, *sel, key++, ValueView::Integer(v));
        REQUIRE(got.Type() == ValueType::Integer);
        CHECK(got.AsInteger() == v);
    }
}

template <class F> void RealRoundTrip()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v REAL)");
    UniquePtr<Statement> ins = Prep(*s.conn, "INSERT INTO kv (k, v) VALUES (?, ?)");
    UniquePtr<Statement> sel = Prep(*s.conn, "SELECT v FROM kv WHERE k = ?");
    const f64 values[] = {0.0,
                          -0.0,
                          1.5,
                          -2.25,
                          3.141592653589793,
                          Limits<f64>::Max(),
                          Limits<f64>::Lowest(),
                          Limits<f64>::Min(),
                          Limits<f64>::DenormMin(),
                          Limits<f64>::Infinity(),
                          -Limits<f64>::Infinity()};
    i64 key = 1;
    for (const f64 v : values)
    {
        const ValueView got = Echo(*ins, *sel, key++, ValueView::Real(v));
        REQUIRE(got.Type() == ValueType::Real);
        if (v == 0.0)
        {
            CHECK(got.AsReal() == 0.0);  // the sign of zero may be lost: SQLite stores -0.0 as the integer 0
                                         // (documented on database::ValueType::Real)
        }
        else
        {
            CHECK(got == ValueView::Real(v));  // bitwise
        }
    }
}

template <class F> void TextRoundTrip()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v TEXT)");
    UniquePtr<Statement> ins = Prep(*s.conn, "INSERT INTO kv (k, v) VALUES (?, ?)");
    UniquePtr<Statement> sel = Prep(*s.conn, "SELECT v FROM kv WHERE k = ?");
    const char with_nul[] = {'a', '\0', 'b'};
    const StringView values[] = {StringView(""), StringView("plain"),
                                 StringView("gr\xC3\xBC\xC3\x9F"
                                            "e \xE2\x9C\x93 \xF0\x9F\x98\x80"),
                                 StringView(with_nul, 3), StringView("it's \"quoted\"; DROP TABLE kv; --")};
    i64 key = 1;
    for (const StringView v : values)
    {
        const ValueView got = Echo(*ins, *sel, key++, ValueView::Text(v));
        REQUIRE(got.Type() == ValueType::Text);
        CHECK(got == ValueView::Text(v));
    }
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM kv") == 5);  // the "injection" text was only data
}

template <class F> void BlobRoundTrip()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v BLOB)");
    UniquePtr<Statement> ins = Prep(*s.conn, "INSERT INTO kv (k, v) VALUES (?, ?)");
    UniquePtr<Statement> sel = Prep(*s.conn, "SELECT v FROM kv WHERE k = ?");
    Vector<byte> big;
    big.Resize(1u << 20);
    u32 state = 12345;
    for (usize i = 0; i < big.Size(); ++i)
    {
        state = state * 1664525u + 1013904223u;
        big[i] = static_cast<byte>(state >> 24);
    }
    const byte small[] = {0, 1, 255, 0, 7};
    i64 key = 1;
    {
        const ValueView got = Echo(*ins, *sel, key++, ValueView::Blob(ByteSpan()));
        REQUIRE(got.Type() == ValueType::Blob);  // an empty blob is a blob, not null
        CHECK(got.AsBlob().Size() == 0);
    }
    {
        const ValueView got = Echo(*ins, *sel, key++, ValueView::Blob(ByteSpan(small, 5)));
        CHECK(got == ValueView::Blob(ByteSpan(small, 5)));
    }
    {
        const ValueView got = Echo(*ins, *sel, key++, ValueView::Blob(ByteSpan(big.Data(), big.Size())));
        CHECK(got == ValueView::Blob(ByteSpan(big.Data(), big.Size())));
    }
}

template <class F> void NullEmptyTextAndEmptyBlobDiffer()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, t TEXT, b BLOB)");
    UniquePtr<Statement> ins = Prep(*s.conn, "INSERT INTO kv (k, t, b) VALUES (?, ?, ?)");
    REQUIRE(ins->Bind(1, ValueView::Integer(1)).Ok());
    REQUIRE(ins->Bind(2, ValueView::Null()).Ok());
    REQUIRE(ins->Bind(3, ValueView::Null()).Ok());
    REQUIRE(StepToEnd(*ins).Ok());
    REQUIRE(ins->Reset().Ok());
    REQUIRE(ins->Bind(1, ValueView::Integer(2)).Ok());
    REQUIRE(ins->Bind(2, ValueView::Text(StringView())).Ok());
    REQUIRE(ins->Bind(3, ValueView::Blob(ByteSpan())).Ok());
    REQUIRE(StepToEnd(*ins).Ok());
    UniquePtr<Statement> sel = Prep(*s.conn, "SELECT t, b FROM kv ORDER BY k");
    REQUIRE(StepRow(*sel));
    CHECK(sel->Column(0).Type() == ValueType::Null);
    CHECK(sel->Column(1).Type() == ValueType::Null);
    REQUIRE(StepRow(*sel));
    CHECK(sel->Column(0).Type() == ValueType::Text);
    CHECK(sel->Column(1).Type() == ValueType::Blob);
    CHECK_FALSE(StepRow(*sel));
}

// --- statements
// ---------------------------------------------------------------------------------------------------------------------

template <class F> void BindIndexOutOfRange()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v INTEGER)");
    UniquePtr<Statement> ins = Prep(*s.conn, "INSERT INTO kv (k, v) VALUES (?, ?)");
    CHECK(ins->ParameterCount() == 2);
    CHECK(ins->Bind(0, ValueView::Integer(1)).Is(ErrorCode::Misuse));
    CHECK(ins->Bind(3, ValueView::Integer(1)).Is(ErrorCode::Misuse));
    CHECK(ins->Bind(2, ValueView::Integer(1)).Ok());
}

template <class F> void NamedParameters()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v INTEGER)");
    UniquePtr<Statement> ins = Prep(*s.conn, "INSERT INTO kv (k, v) VALUES (:key, :value)");
    u32 index = 0;
    if (!s.conn->Supports(Feature::NamedParameters))
    {
        CHECK(ins->ParameterIndex(":key", &index).Is(ErrorCode::Unsupported));
        return;
    }
    REQUIRE(ins->ParameterIndex(":value", &index).Ok());
    CHECK(index == 2);
    REQUIRE(ins->ParameterIndex(":key", &index).Ok());
    CHECK(index == 1);
    CHECK(ins->ParameterIndex(":nothing", &index).Is(ErrorCode::NotFound));
    REQUIRE(ins->Bind(1, ValueView::Integer(7)).Ok());
    REQUIRE(ins->Bind(2, ValueView::Integer(70)).Ok());
    REQUIRE(StepToEnd(*ins).Ok());
    CHECK(Scalar(*s.conn, "SELECT v FROM kv WHERE k = 7") == 70);
}

template <class F> void StepAndReset()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v INTEGER)");
    Run(*s.conn, "INSERT INTO kv (k, v) VALUES (1, 10), (2, 20), (3, 30)");
    UniquePtr<Statement> sel = Prep(*s.conn, "SELECT k, v FROM kv ORDER BY k");
    CHECK(sel->ColumnCount() == 2);
    for (int pass = 0; pass < 2; ++pass)
    {
        i64 sum = 0;
        u32 rows = 0;
        while (StepRow(*sel))
        {
            sum += sel->Column(1).AsInteger();
            ++rows;
        }
        CHECK(rows == 3);
        CHECK(sum == 60);
        REQUIRE(sel->Reset().Ok());
    }
}

template <class F> void StepAfterDoneNeedsReset()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY)");
    UniquePtr<Statement> sel = Prep(*s.conn, "SELECT k FROM kv");
    CHECK_FALSE(StepRow(*sel));
    bool row = true;
    CHECK(sel->Step(&row).Is(ErrorCode::Misuse));
    REQUIRE(sel->Reset().Ok());
    CHECK(sel->Step(&row).Ok());
}

template <class F> void ClearBindings()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v INTEGER)");
    UniquePtr<Statement> ins = Prep(*s.conn, "INSERT INTO kv (k, v) VALUES (?, ?)");
    REQUIRE(ins->Bind(1, ValueView::Integer(1)).Ok());
    REQUIRE(ins->Bind(2, ValueView::Integer(99)).Ok());
    REQUIRE(ins->ClearBindings().Ok());
    REQUIRE(ins->Bind(1, ValueView::Integer(2)).Ok());
    REQUIRE(StepToEnd(*ins).Ok());
    UniquePtr<Statement> sel = Prep(*s.conn, "SELECT v FROM kv WHERE k = 2");
    REQUIRE(StepRow(*sel));
    CHECK(sel->Column(0).IsNull());
}

template <class F> void SyntaxErrors()
{
    Session<F> s;
    UniquePtr<Statement> st;
    CHECK(s.conn->Prepare("SELEKT nothing", &st).Is(ErrorCode::Syntax));
    CHECK(st == nullptr);
    CHECK(s.conn->Execute("CREATE TABLE (").Is(ErrorCode::Syntax));
    CHECK(s.conn->Prepare("SELECT k FROM missing_table", &st).Failed());
}

template <class F> void StatementReuse()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v INTEGER)");
    UniquePtr<Statement> ins = Prep(*s.conn, "INSERT INTO kv (k, v) VALUES (?, ?)");
    for (i64 i = 1; i <= 1000; ++i)
    {
        REQUIRE(ins->Reset().Ok());
        REQUIRE(ins->Bind(1, ValueView::Integer(i)).Ok());
        REQUIRE(ins->Bind(2, ValueView::Integer(i * 2)).Ok());
        REQUIRE(StepToEnd(*ins).Ok());
    }
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM kv") == 1000);
    CHECK(Scalar(*s.conn, "SELECT v FROM kv WHERE k = 500") == 1000);
}

// --- constraints and rows
// ---------------------------------------------------------------------------------------------------------------------

template <class F> void PrimaryKeyConstraint()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v INTEGER)");
    Run(*s.conn, "INSERT INTO kv (k, v) VALUES (1, 10)");
    UniquePtr<Statement> st = Prep(*s.conn, "INSERT INTO kv (k, v) VALUES (1, 11)");
    bool row = false;
    CHECK(st->Step(&row).Is(ErrorCode::Constraint));
    CHECK(Scalar(*s.conn, "SELECT v FROM kv WHERE k = 1") == 10);  // the refused statement changed nothing
}

template <class F> void UniqueAndNotNullConstraints()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE u (k INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE)");
    Run(*s.conn, "INSERT INTO u (k, name) VALUES (1, 'a')");
    CHECK(s.conn->Execute("INSERT INTO u (k, name) VALUES (2, 'a')").Is(ErrorCode::Constraint));
    CHECK(s.conn->Execute("INSERT INTO u (k, name) VALUES (3, NULL)").Is(ErrorCode::Constraint));
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM u") == 1);
}

template <class F> void CompositePrimaryKey()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE c (a INTEGER NOT NULL, b INTEGER NOT NULL, v INTEGER, PRIMARY KEY (a, b))");
    Run(*s.conn, "INSERT INTO c (a, b, v) VALUES (1, 1, 1), (1, 2, 2), (2, 1, 3)");
    CHECK(s.conn->Execute("INSERT INTO c (a, b, v) VALUES (1, 2, 9)").Is(ErrorCode::Constraint));
    CHECK(Scalar(*s.conn, "SELECT v FROM c WHERE a = 1 AND b = 2") == 2);
}

template <class F> void ForeignKeys()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE parent (k INTEGER PRIMARY KEY)");
    Run(*s.conn, "CREATE TABLE child (k INTEGER PRIMARY KEY, p INTEGER REFERENCES parent(k) ON DELETE CASCADE)");
    Run(*s.conn, "INSERT INTO parent (k) VALUES (1), (2)");
    if (!s.conn->Supports(Feature::ForeignKeys))
    {
        return;  // nothing is promised without the feature
    }
    CHECK(s.conn->Execute("INSERT INTO child (k, p) VALUES (1, 99)").Is(ErrorCode::Constraint));
    Run(*s.conn, "INSERT INTO child (k, p) VALUES (1, 1), (2, 1), (3, 2)");
    Run(*s.conn, "DELETE FROM parent WHERE k = 1");
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM child") == 1);  // the cascade removed the children of parent 1
}

template <class F> void InsertOrReplace()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v INTEGER)");
    Run(*s.conn, "INSERT INTO kv (k, v) VALUES (1, 10)");
    Run(*s.conn, "INSERT OR REPLACE INTO kv (k, v) VALUES (1, 11)");
    CHECK(Scalar(*s.conn, "SELECT v FROM kv WHERE k = 1") == 11);
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM kv") == 1);
}

template <class F> void LastInsertRowIdAndChangedRows()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v INTEGER)");
    Run(*s.conn, "INSERT INTO kv (k, v) VALUES (41, 1)");
    CHECK(s.conn->LastInsertRowId() == 41);
    CHECK(s.conn->ChangedRows() == 1);
    Run(*s.conn, "INSERT INTO kv (k, v) VALUES (50, 1), (51, 1), (52, 1)");
    CHECK(s.conn->ChangedRows() == 3);
    CHECK(s.conn->LastInsertRowId() == 52);
    Run(*s.conn, "UPDATE kv SET v = 2 WHERE k > 41");
    CHECK(s.conn->ChangedRows() == 3);
    Run(*s.conn, "DELETE FROM kv WHERE k = 50");
    CHECK(s.conn->ChangedRows() == 1);
    Run(*s.conn, "DELETE FROM kv WHERE k = 12345");
    CHECK(s.conn->ChangedRows() == 0);
}

template <class F> void SelectWhereOrderLimit()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v INTEGER, name TEXT)");
    Run(*s.conn,
        "INSERT INTO kv (k, v, name) VALUES (1, 30, 'c'), (2, 10, 'a'), (3, 20, 'b'), (4, 40, 'd'), (5, 20, 'e')");
    UniquePtr<Statement> st = Prep(*s.conn,
                                   "SELECT k FROM kv WHERE v >= ? AND v < ? ORDER BY v DESC, k ASC LIMIT 2 OFFSET 1");
    REQUIRE(st->Bind(1, ValueView::Integer(20)).Ok());
    REQUIRE(st->Bind(2, ValueView::Integer(41)).Ok());
    // v desc: 4(40), 1(30), 3(20), 5(20): offset 1, limit 2 gives 1 then 3
    REQUIRE(StepRow(*st));
    CHECK(st->Column(0).AsInteger() == 1);
    REQUIRE(StepRow(*st));
    CHECK(st->Column(0).AsInteger() == 3);
    CHECK_FALSE(StepRow(*st));
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM kv WHERE name > 'a'") == 4);
}

template <class F> void IsNullQueries()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v INTEGER)");
    Run(*s.conn, "INSERT INTO kv (k, v) VALUES (1, NULL), (2, 5), (3, NULL)");
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM kv WHERE v IS NULL") == 2);
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM kv WHERE v IS NOT NULL") == 1);
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM kv WHERE v = 5") == 1);
}

template <class F> void IndexesAndUniqueIndexes()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v INTEGER, w INTEGER)");
    Run(*s.conn, "CREATE INDEX kv_v ON kv (v)");
    Run(*s.conn, "CREATE UNIQUE INDEX kv_w ON kv (w)");
    Run(*s.conn, "INSERT INTO kv (k, v, w) VALUES (1, 7, 1), (2, 7, 2)");
    CHECK(s.conn->Execute("INSERT INTO kv (k, v, w) VALUES (3, 7, 2)").Is(ErrorCode::Constraint));
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM kv WHERE v = 7") == 2);
    Run(*s.conn, "CREATE INDEX IF NOT EXISTS kv_v ON kv (v)");
}

template <class F> void ManyRows()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v INTEGER)");
    {
        Transaction tx(*s.conn);
        REQUIRE(tx.BeginStatus().Ok());
        UniquePtr<Statement> ins = Prep(*s.conn, "INSERT INTO kv (k, v) VALUES (?, ?)");
        for (i64 i = 1; i <= 10000; ++i)
        {
            REQUIRE(ins->Reset().Ok());
            REQUIRE(ins->Bind(1, ValueView::Integer(i)).Ok());
            REQUIRE(ins->Bind(2, ValueView::Integer(i % 100)).Ok());
            REQUIRE(StepToEnd(*ins).Ok());
        }
        REQUIRE(tx.Commit().Ok());
    }
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM kv") == 10000);
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM kv WHERE v = 7") == 100);
}

// --- transactions
// ---------------------------------------------------------------------------------------------------------------------

template <class F> void CommitAndRollback()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v INTEGER)");
    REQUIRE(s.conn->Begin(TransactionKind::Immediate).Ok());
    CHECK(s.conn->InTransaction());
    Run(*s.conn, "INSERT INTO kv (k, v) VALUES (1, 1)");
    REQUIRE(s.conn->Commit().Ok());
    CHECK_FALSE(s.conn->InTransaction());
    REQUIRE(s.conn->Begin(TransactionKind::Deferred).Ok());
    Run(*s.conn, "INSERT INTO kv (k, v) VALUES (2, 2)");
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM kv") == 2);  // the writer sees its own change
    REQUIRE(s.conn->Rollback().Ok());
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM kv") == 1);
}

template <class F> void TransactionGuard()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v INTEGER)");
    {
        Transaction tx(*s.conn);
        REQUIRE(tx.BeginStatus().Ok());
        Run(*s.conn, "INSERT INTO kv (k, v) VALUES (1, 1)");
    }  // destroyed without Commit: rolled back
    CHECK_FALSE(s.conn->InTransaction());
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM kv") == 0);
    {
        Transaction tx(*s.conn);
        REQUIRE(tx.BeginStatus().Ok());
        Run(*s.conn, "INSERT INTO kv (k, v) VALUES (2, 2)");
        REQUIRE(tx.Commit().Ok());
        CHECK(tx.Commit().Is(ErrorCode::Misuse));
    }
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM kv") == 1);
}

template <class F> void TransactionMisuse()
{
    Session<F> s;
    CHECK(s.conn->Commit().Is(ErrorCode::Misuse));
    CHECK(s.conn->Rollback().Is(ErrorCode::Misuse));
    REQUIRE(s.conn->Begin(TransactionKind::Deferred).Ok());
    CHECK(s.conn->Begin(TransactionKind::Deferred).Is(ErrorCode::Misuse));
    REQUIRE(s.conn->Rollback().Ok());
}

template <class F> void Savepoints()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY)");
    if (!s.conn->Supports(Feature::Savepoints))
    {
        CHECK(s.conn->Savepoint("a").Is(ErrorCode::Unsupported));
        return;
    }
    REQUIRE(s.conn->Begin(TransactionKind::Immediate).Ok());
    Run(*s.conn, "INSERT INTO kv (k) VALUES (1)");
    REQUIRE(s.conn->Savepoint("one").Ok());
    Run(*s.conn, "INSERT INTO kv (k) VALUES (2)");
    REQUIRE(s.conn->Savepoint("two").Ok());
    Run(*s.conn, "INSERT INTO kv (k) VALUES (3)");
    REQUIRE(s.conn->Savepoint("three").Ok());
    Run(*s.conn, "INSERT INTO kv (k) VALUES (4)");
    REQUIRE(s.conn->RollbackToSavepoint("three").Ok());  // 4 is gone, the savepoint stays
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM kv") == 3);
    REQUIRE(s.conn->RollbackToSavepoint("two").Ok());  // 3 is gone
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM kv") == 2);
    REQUIRE(s.conn->ReleaseSavepoint("one").Ok());  // keeps 2, forgets "two" inside it
    CHECK(s.conn->RollbackToSavepoint("two").Is(ErrorCode::Misuse));
    CHECK(s.conn->ReleaseSavepoint("nothing").Is(ErrorCode::Misuse));
    CHECK(s.conn->Savepoint("not an identifier; DROP").Is(ErrorCode::Misuse));
    REQUIRE(s.conn->Commit().Ok());
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM kv") == 2);
}

template <class F> void FailedStatementKeepsTransactionUsable()
{
    Session<F> s;
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v INTEGER)");
    REQUIRE(s.conn->Begin(TransactionKind::Immediate).Ok());
    Run(*s.conn, "INSERT INTO kv (k, v) VALUES (1, 1)");
    CHECK(s.conn->Execute("INSERT INTO kv (k, v) VALUES (1, 2)").Is(ErrorCode::Constraint));
    CHECK(s.conn->InTransaction());
    Run(*s.conn, "INSERT INTO kv (k, v) VALUES (2, 2)");
    REQUIRE(s.conn->Commit().Ok());
    CHECK(Scalar(*s.conn, "SELECT count(*) FROM kv") == 2);
}

template <class F> void SchemaVersionPersists()
{
    Session<F> s;
    REQUIRE(s.conn->WriteSchemaVersion(7).Ok());
    i64 version = 0;
    if constexpr (F::kSharedLocation)
    {
        UniquePtr<Connection> other = s.Another();
        REQUIRE(other->SchemaVersion(&version).Ok());
        CHECK(version == 7);
    }
    REQUIRE(s.conn->Begin(TransactionKind::Immediate).Ok());
    REQUIRE(s.conn->WriteSchemaVersion(8).Ok());
    REQUIRE(s.conn->Rollback().Ok());
    REQUIRE(s.conn->SchemaVersion(&version).Ok());
    CHECK(version == 7);  // the version is part of the transaction
}

// --- several connections
// ---------------------------------------------------------------------------------------------------------------------

template <class F> void TwoConnectionsSeeCommittedDataOnly()
{
    Session<F> s;
    if (!F::kSharedLocation)
    {
        SKIP("the fixture " << F::Name() << " has no database two connections can share");
    }
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v INTEGER)");
    UniquePtr<Connection> other = s.Another();
    REQUIRE(s.conn->Begin(TransactionKind::Immediate).Ok());
    Run(*s.conn, "INSERT INTO kv (k, v) VALUES (1, 1)");
    if (s.conn->Supports(Feature::ConcurrentReaders))
    {
        CHECK(Scalar(*other, "SELECT count(*) FROM kv") == 0);  // the reader proceeds and sees the committed state
    }
    REQUIRE(s.conn->Commit().Ok());
    CHECK(Scalar(*other, "SELECT count(*) FROM kv") == 1);
}

template <class F> void SecondWriterGetsBusy()
{
    Session<F> s;
    if (!F::kSharedLocation)
    {
        SKIP("the fixture " << F::Name() << " has no database two connections can share");
    }
    Run(*s.conn, "CREATE TABLE kv (k INTEGER PRIMARY KEY, v INTEGER)");
    OpenOptions quick = s.fixture.Options();
    quick.busy_timeout_ms = 50;
    UniquePtr<Connection> other;
    REQUIRE(s.fixture.Db().Open(quick, &other).Ok());
    REQUIRE(s.conn->Begin(TransactionKind::Immediate).Ok());
    Run(*s.conn, "INSERT INTO kv (k, v) VALUES (1, 1)");
    CHECK(other->Begin(TransactionKind::Immediate).Is(ErrorCode::Busy));
    CHECK(other->Execute("INSERT INTO kv (k, v) VALUES (2, 2)").Is(ErrorCode::Busy));
    REQUIRE(s.conn->Rollback().Ok());
    REQUIRE(other->Begin(TransactionKind::Immediate).Ok());  // the lock is free again
    REQUIRE(other->Rollback().Ok());
}

// --- features
// ---------------------------------------------------------------------------------------------------------------------

template <class F> void SupportsAgreesWithBehaviour()
{
    Session<F> s;
    CHECK(s.conn->Supports(Feature::Savepoints) == s.fixture.Db().Supports(Feature::Savepoints));
    // WithoutRowId: present means the table is created, absent means it is refused (never silently ignored into a
    // different table).
    const Status without_rowid = s.conn->Execute("CREATE TABLE w (k INTEGER PRIMARY KEY, v INTEGER) WITHOUT ROWID");
    CHECK(without_rowid.Ok() == s.conn->Supports(Feature::WithoutRowId));
    if (without_rowid.Failed())
    {
        CHECK((without_rowid.Is(ErrorCode::Unsupported) || without_rowid.Is(ErrorCode::Syntax)));
    }
    // AddColumn.
    Run(*s.conn, "CREATE TABLE a (k INTEGER PRIMARY KEY)");
    const Status add = s.conn->Execute("ALTER TABLE a ADD COLUMN extra INTEGER");
    CHECK(add.Ok() == s.conn->Supports(Feature::AddColumn));
    if (add.Failed())
    {
        CHECK((add.Is(ErrorCode::Unsupported) || add.Is(ErrorCode::Syntax)));
    }
    else
    {
        Run(*s.conn, "INSERT INTO a (k, extra) VALUES (1, 2)");
        CHECK(Scalar(*s.conn, "SELECT extra FROM a WHERE k = 1") == 2);
    }
    // WriteAheadLog: a connection asking for it either gets it or is told it is unsupported.
    OpenOptions wal = s.fixture.Options();
    wal.journal_mode = JournalMode::WriteAhead;
    UniquePtr<Connection> other;
    const Status opened = s.fixture.Db().Open(wal, &other);
    if (s.conn->Supports(Feature::WriteAheadLog))
    {
        CHECK(opened.Ok());
    }
    else
    {
        CHECK(opened.Is(ErrorCode::Unsupported));
    }
}

// --- the registration macro
// ---------------------------------------------------------------------------------------------------------------------

#define DATABASE_CONFORMANCE_CASE(FIXTURE, TITLE, FUNCTION)                                                            \
    TEST_CASE("Database conformance (" #FIXTURE "): " TITLE, "[database][conformance]")                                \
    {                                                                                                                  \
        ::engine_placeholder::database::conformance::FUNCTION<FIXTURE>();                                              \
    }

}  // namespace engine_placeholder::database::conformance

// Registers every case of the suite for one fixture.
#define DATABASE_CONFORMANCE_CASES(FIXTURE)                                                                            \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "a database opens, reports its schema version and reopens", OpenAndClose)       \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "integers round trip at their limits", IntegerRoundTrip)                        \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "reals round trip bitwise at the extremes, and zero keeps its value",           \
                              RealRoundTrip)                                                                           \
    DATABASE_CONFORMANCE_CASE(                                                                                         \
        FIXTURE, "text round trips with multi-byte characters, an embedded NUL and quote characters", TextRoundTrip)   \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "blobs round trip, empty and 1 MiB", BlobRoundTrip)                             \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "null, empty text and empty blob are three different values",                   \
                              NullEmptyTextAndEmptyBlobDiffer)                                                         \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "a bind index out of range is a misuse", BindIndexOutOfRange)                   \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "named parameters work or are reported unsupported", NamedParameters)           \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "Step and Reset walk the rows twice", StepAndReset)                             \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "a Step after done needs a Reset", StepAfterDoneNeedsReset)                     \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "ClearBindings binds null", ClearBindings)                                      \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "invalid SQL is a syntax error", SyntaxErrors)                                  \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "a statement is reused 1,000 times", StatementReuse)                            \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "a duplicate primary key is a constraint error that changes nothing",           \
                              PrimaryKeyConstraint)                                                                    \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "unique and not-null rules are enforced", UniqueAndNotNullConstraints)          \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "a composite primary key is enforced", CompositePrimaryKey)                     \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "foreign keys are enforced and cascade when supported", ForeignKeys)            \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "INSERT OR REPLACE replaces the row", InsertOrReplace)                          \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "the last insert key and the changed row counts",                               \
                              LastInsertRowIdAndChangedRows)                                                           \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "SELECT with WHERE, ORDER BY, LIMIT and OFFSET", SelectWhereOrderLimit)         \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "IS NULL and IS NOT NULL", IsNullQueries)                                       \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "indexes and unique indexes", IndexesAndUniqueIndexes)                          \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "10,000 rows in one transaction", ManyRows)                                     \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "commit keeps and rollback undoes", CommitAndRollback)                          \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "the transaction guard rolls back unless committed", TransactionGuard)          \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "commit and rollback outside a transaction, and a nested begin, are misuse",    \
                              TransactionMisuse)                                                                       \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "savepoints nest three deep", Savepoints)                                       \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "a failed statement leaves the transaction usable",                             \
                              FailedStatementKeepsTransactionUsable)                                                   \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "the schema version persists and is transactional", SchemaVersionPersists)      \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "two connections see committed data only", TwoConnectionsSeeCommittedDataOnly)  \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "a second writer gets Busy after its timeout", SecondWriterGetsBusy)            \
    DATABASE_CONFORMANCE_CASE(FIXTURE, "Supports agrees with behaviour", SupportsAgreesWithBehaviour)
