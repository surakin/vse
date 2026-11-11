// The database interface's own types (Phase 19 Day 252): Status, SqlText, ValueView, Value and the Transaction guard
// (with a recording fake connection).

#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "database/database.h"
#include "lang/limits.h"
#include "lang/traits.h"

namespace engine_placeholder::database
{
namespace
{

TEST_CASE("Status: success, codes and a cut message", "[database]")
{
    CHECK(Status::Success().Ok());
    CHECK_FALSE(Status::Success().Failed());
    const Status busy = Status::Of(ErrorCode::Busy);
    CHECK(busy.Failed());
    CHECK(busy.Is(ErrorCode::Busy));
    CHECK_FALSE(busy.Is(ErrorCode::Locked));
    char long_text[300];
    for (char& c : long_text)
    {
        c = 'x';
    }
    const Status s = Status::Make(ErrorCode::Io, 7, StringView(long_text, sizeof(long_text)));
    CHECK(s.BackendCode() == 7);
    CHECK(s.Message().Length() == Status::kMaxMessageBytes);
    CHECK(StringView(ErrorCodeName(ErrorCode::Constraint)) == StringView("constraint"));
}

TEST_CASE("SqlText: only a literal converts, and the text is viewable", "[database]")
{
    static_assert(IsConvertible<const char (&)[4], SqlText>, "a literal converts");
    static_assert(!IsConstructible<SqlText, StringView>, "a StringView must not become SQL");
    constexpr SqlText sql = "SELECT 1";
    CHECK(sql.View() == StringView("SELECT 1"));
    CHECK(SqlText::Unchecked(StringView("SAVEPOINT a")).View() == StringView("SAVEPOINT a"));
}

TEST_CASE("ValueView: types, accessors and bitwise equality", "[database]")
{
    CHECK(ValueView::Null().IsNull());
    CHECK(ValueView::Integer(5).AsInteger() == 5);
    CHECK(ValueView::Real(1.5).AsReal() == 1.5);
    CHECK(ValueView::Text(StringView("hi")).AsText() == StringView("hi"));
    CHECK(ValueView::Integer(1) == ValueView::Integer(1));
    CHECK_FALSE(ValueView::Integer(1) == ValueView::Real(1.0));  // the types differ
    CHECK_FALSE(ValueView::Real(0.0) == ValueView::Real(-0.0));  // bitwise
    CHECK(ValueView::Real(Limits<f64>::QuietNaN()) == ValueView::Real(Limits<f64>::QuietNaN()));
    CHECK_FALSE(ValueView::Text(StringView("")) == ValueView::Null());
    CHECK_FALSE(ValueView::Text(StringView("")) == ValueView::Blob(ByteSpan()));
    CHECK(StringView(ValueTypeName(ValueType::Blob)) == StringView("blob"));
}

TEST_CASE("Value: owns a copy of text and bytes", "[database]")
{
    char text[] = "owned";
    const Value v = Value::Text(StringView(text, 5));
    text[0] = 'X';
    CHECK(v.View().AsText() == StringView("owned"));
    const byte bytes[] = {1, 2, 3};
    const Value b = Value::Blob(ByteSpan(bytes, 3));
    CHECK(b.View() == ValueView::Blob(ByteSpan(bytes, 3)));
    const Value copy = Value::From(ValueView::Integer(9));
    CHECK(copy == Value::Integer(9));
    CHECK(Value() == Value::Null());
}

// A connection that records the transaction calls.
class RecordingConnection final : public Connection
{
public:
    Vector<char> log;  // 'B' begin, 'C' commit, 'R' rollback
    Status begin_result = Status::Success();
    Status commit_result = Status::Success();
    bool in_tx = false;

    bool Supports(Feature) const override { return false; }
    Status Prepare(SqlText, UniquePtr<Statement>*) override { return Status::Of(ErrorCode::Unsupported); }
    Status Execute(SqlText) override { return Status::Success(); }
    Status Begin(TransactionKind) override
    {
        log.Push('B');
        if (begin_result.Ok())
        {
            in_tx = true;
        }
        return begin_result;
    }
    Status Commit() override
    {
        log.Push('C');
        if (commit_result.Ok())
        {
            in_tx = false;
        }
        return commit_result;
    }
    Status Rollback() override
    {
        log.Push('R');
        in_tx = false;
        return Status::Success();
    }
    bool InTransaction() const override { return in_tx; }
    Status Savepoint(StringView) override { return Status::Of(ErrorCode::Unsupported); }
    Status ReleaseSavepoint(StringView) override { return Status::Of(ErrorCode::Unsupported); }
    Status RollbackToSavepoint(StringView) override { return Status::Of(ErrorCode::Unsupported); }
    i64 LastInsertRowId() const override { return 0; }
    i64 ChangedRows() const override { return 0; }
    Status SchemaVersion(i64* out) override
    {
        *out = 0;
        return Status::Success();
    }
    Status WriteSchemaVersion(i64) override { return Status::Success(); }
};

bool LogIs(const RecordingConnection& c, const char* expected)
{
    usize n = 0;
    while (expected[n] != '\0')
    {
        ++n;
    }
    if (c.log.Size() != n)
    {
        return false;
    }
    for (usize i = 0; i < n; ++i)
    {
        if (c.log[i] != expected[i])
        {
            return false;
        }
    }
    return true;
}

TEST_CASE("Transaction: commit, scope exit and failures", "[database]")
{
    SECTION("committed: no rollback")
    {
        RecordingConnection c;
        {
            Transaction tx(c);
            REQUIRE(tx.BeginStatus().Ok());
            REQUIRE(tx.Commit().Ok());
        }
        CHECK(LogIs(c, "BC"));
    }
    SECTION("left uncommitted: rolled back")
    {
        RecordingConnection c;
        {
            Transaction tx(c);
        }
        CHECK(LogIs(c, "BR"));
    }
    SECTION("a failed begin: nothing to roll back, Commit is misuse")
    {
        RecordingConnection c;
        c.begin_result = Status::Of(ErrorCode::Busy);
        {
            Transaction tx(c);
            CHECK(tx.BeginStatus().Is(ErrorCode::Busy));
            CHECK(tx.Commit().Is(ErrorCode::Misuse));
        }
        CHECK(LogIs(c, "B"));
    }
    SECTION("a failed commit leaves the transaction open, so the destructor rolls back")
    {
        RecordingConnection c;
        c.commit_result = Status::Of(ErrorCode::Busy);
        {
            Transaction tx(c);
            CHECK(tx.Commit().Is(ErrorCode::Busy));
        }
        CHECK(LogIs(c, "BCR"));
    }
    SECTION("a second commit is misuse and does not reach the connection")
    {
        RecordingConnection c;
        Transaction tx(c);
        REQUIRE(tx.Commit().Ok());
        CHECK(tx.Commit().Is(ErrorCode::Misuse));
        CHECK(LogIs(c, "BC"));
    }
}

}  // namespace
}  // namespace engine_placeholder::database
