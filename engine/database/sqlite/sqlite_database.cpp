#include "database/sqlite/sqlite_database.h"

#include <sqlite3.h>

#include "containers/shared_ptr.h"
#include "containers/unique_ptr.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "database/sqlite/sqlite_os.h"
#include "io/file_system.h"
#include "io/path.h"
#include "lang/scalar.h"
#include "log/assert.h"
#include "containers/vector.h"
#include "sync/atomic.h"

namespace engine_placeholder::database
{
namespace
{

// --- result codes
// ---------------------------------------------------------------------------------------------------------------------

ErrorCode MapResultCode(int rc)
{
    switch (rc & 0xff)
    {
    case SQLITE_OK:
    case SQLITE_ROW:
    case SQLITE_DONE: return ErrorCode::Ok;
    case SQLITE_BUSY: return ErrorCode::Busy;
    case SQLITE_LOCKED: return ErrorCode::Locked;
    case SQLITE_READONLY: return ErrorCode::ReadOnly;
    case SQLITE_INTERRUPT: return ErrorCode::Interrupted;
    case SQLITE_IOERR:
    case SQLITE_CANTOPEN: return ErrorCode::Io;
    case SQLITE_CORRUPT:
    case SQLITE_NOTADB: return ErrorCode::Corrupt;
    case SQLITE_NOTFOUND: return ErrorCode::NotFound;
    case SQLITE_FULL: return ErrorCode::Full;
    case SQLITE_TOOBIG: return ErrorCode::TooBig;
    case SQLITE_CONSTRAINT:
    case SQLITE_MISMATCH: return ErrorCode::Constraint;
    case SQLITE_MISUSE:
    case SQLITE_RANGE: return ErrorCode::Misuse;
    default: return ErrorCode::Other;
    }
}

// sqlite3_exec callback of PRAGMA journal_mode: remembers whether the answer is "wal".
int NoteJournalMode(void* user, int columns, char** values, char**)
{
    if (columns > 0 && values[0] != nullptr)
    {
        *static_cast<bool*>(user) = CStrCompare(values[0], "wal") == 0;
    }
    return 0;
}

// sqlite3_exec callback of PRAGMA quick_check / integrity_check: clears the flag when the first row is not "ok".
int NoteIntegrity(void* user, int columns, char** values, char**)
{
    if (columns > 0 && values[0] != nullptr && CStrCompare(values[0], "ok") != 0)
    {
        *static_cast<bool*>(user) = false;
    }
    return 0;
}

Status StatusFromDb(sqlite3* db, int rc)
{
    const char* message = db != nullptr ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
    return Status::Make(MapResultCode(rc), rc, StringView(message, CStrLength(message)));
}

Status Failure(ErrorCode code, const char* message)
{
    return Status::Make(code, 0, StringView(message, CStrLength(message)));
}

// SQLite reports a syntax error, an unknown table or column and a bad savepoint name all as SQLITE_ERROR; only the
// message tells them apart.
ErrorCode ClassifyPrepareError(int rc, const char* message)
{
    if (rc != SQLITE_ERROR)
    {
        return MapResultCode(rc);
    }
    if (CStrFind(message, "syntax error") != nullptr || CStrFind(message, "incomplete input") != nullptr)
    {
        return ErrorCode::Syntax;
    }
    if (CStrCompareN(message, "no such ", 8) == 0)
    {
        return ErrorCode::NotFound;
    }
    return ErrorCode::Other;
}

// --- the shared connection handle
// ----------------------------------------------------------------------------------------------------------------

// The database connection outlives its Connection object while a statement still exists, and the statements check
// `open` before they use it.
struct SqliteHandle
{
    sqlite3* db = nullptr;
    bool open = true;
    u32 cache_capacity = 0;      // finished statements kept for reuse (0 keeps none)
    Vector<sqlite3_stmt*> idle;  // reset statements, oldest first; keyed by the text SQLite keeps (`sqlite3_sql`)
    Atomic<u32> users{
        0};  // threads inside Prepare or Step: a debug check that a connection is used by one thread at a time
    SqliteHandle() = default;
    SqliteHandle(const SqliteHandle&) = delete;
    SqliteHandle& operator=(const SqliteHandle&) = delete;
    ~SqliteHandle()
    {
        ClearIdle();
        if (db != nullptr)
        {
            sqlite3_close_v2(db);
        }
    }

    void ClearIdle()
    {
        for (usize i = 0; i < idle.Size(); ++i)
        {
            sqlite3_finalize(idle[i]);
        }
        idle.Clear();
    }

    // Takes back a statement that its owner is done with: reset, unbound and kept for the next Prepare of the same
    // text, or finalized.
    void Release(sqlite3_stmt* stmt)
    {
        if (!open || cache_capacity == 0)
        {
            sqlite3_finalize(stmt);
            return;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        if (idle.Size() >= cache_capacity)
        {
            sqlite3_finalize(idle[0]);
            idle.Remove(0);
        }
        idle.Push(stmt);
    }

    // Removes and returns an idle statement whose text is `text`, or null.
    sqlite3_stmt* TakeIdle(StringView text)
    {
        for (usize i = idle.Size(); i > 0; --i)
        {
            const char* sql = sqlite3_sql(idle[i - 1]);
            if (sql != nullptr && CStrLength(sql) == text.Length() && MemCompare(sql, text.Data(), text.Length()) == 0)
            {
                sqlite3_stmt* found = idle[i - 1];
                idle.Remove(i - 1);
                return found;
            }
        }
        return nullptr;
    }
};

// A debug check (slow checks only): two threads inside one connection's Prepare or Step at once is a data race the
// caller must not make.
class UseGuard
{
public:
#if ENGINE_SLOW_CHECKS
    explicit UseGuard(SqliteHandle* handle) : handle_(handle)
    {
        const u32 before = handle_->users.FetchAdd(1);
        ENGINE_CHECK_SLOW(before == 0, "database: a connection and its statements are used by two threads at once");
    }
    ~UseGuard() { handle_->users.FetchSub(1); }
#else
    explicit UseGuard(SqliteHandle*) {}
#endif
    UseGuard(const UseGuard&) = delete;
    UseGuard& operator=(const UseGuard&) = delete;

#if ENGINE_SLOW_CHECKS

private:
    SqliteHandle* handle_;
#endif
};

Status ClosedConnection()
{
    return Failure(ErrorCode::Misuse, "the connection of this statement is closed");
}

// --- statements
// ---------------------------------------------------------------------------------------------------------------------

class SqliteStatement final : public Statement
{
public:
    SqliteStatement(SharedPtr<SqliteHandle> handle, sqlite3_stmt* stmt) : handle_(Move(handle)), stmt_(stmt) {}
    ~SqliteStatement() override { handle_->Release(stmt_); }

    u32 ParameterCount() const override { return static_cast<u32>(sqlite3_bind_parameter_count(stmt_)); }

    Status ParameterIndex(StringView name, u32* index) const override
    {
        if (!handle_->open)
        {
            return ClosedConnection();
        }
        char buffer[128];
        if (name.Length() == 0 || name.Length() >= sizeof(buffer))
        {
            return Failure(ErrorCode::NotFound, "no such parameter");
        }
        for (usize i = 0; i < name.Length(); ++i)
        {
            buffer[i] = name.Data()[i];
        }
        buffer[name.Length()] = '\0';
        const int found = sqlite3_bind_parameter_index(stmt_, buffer);
        if (found == 0)
        {
            return Failure(ErrorCode::NotFound, "no such parameter");
        }
        *index = static_cast<u32>(found);
        return Status::Success();
    }

    Status Bind(u32 index, const ValueView& value) override
    {
        if (!handle_->open)
        {
            return ClosedConnection();
        }
        if (index < 1 || index > ParameterCount())
        {
            return Failure(ErrorCode::Misuse, "Bind: the parameter index is out of range");
        }
        const int i = static_cast<int>(index);
        int rc = SQLITE_OK;
        switch (value.Type())
        {
        case ValueType::Null: rc = sqlite3_bind_null(stmt_, i); break;
        case ValueType::Integer: rc = sqlite3_bind_int64(stmt_, i, value.AsInteger()); break;
        case ValueType::Real:
            if (IsNaN(value.AsReal()))
            {  // SQLite would store NULL
                return Failure(ErrorCode::Misuse, "Bind: a NaN cannot be stored");
            }
            rc = sqlite3_bind_double(stmt_, i, value.AsReal());
            break;
        case ValueType::Text:
        {
            const StringView text = value.AsText();
            rc = sqlite3_bind_text64(stmt_, i, text.Length() == 0 ? "" : text.Data(),
                                     static_cast<sqlite3_uint64>(text.Length()), SQLITE_TRANSIENT, SQLITE_UTF8);
            break;
        }
        case ValueType::Blob:
        {
            const ByteSpan bytes = value.AsBlob();
            rc = bytes.Size() == 0 ? sqlite3_bind_zeroblob(stmt_, i, 0)
                                   : sqlite3_bind_blob64(stmt_, i, bytes.Data(),
                                                         static_cast<sqlite3_uint64>(bytes.Size()), SQLITE_TRANSIENT);
            break;
        }
        }
        return rc == SQLITE_OK ? Status::Success() : StatusFromDb(handle_->db, rc);
    }

    Status ClearBindings() override
    {
        if (!handle_->open)
        {
            return ClosedConnection();
        }
        sqlite3_clear_bindings(stmt_);
        return Status::Success();
    }

    Status Reset() override
    {
        if (!handle_->open)
        {
            return ClosedConnection();
        }
        sqlite3_reset(stmt_);  // returns the error of the last Step, which that Step already reported
        done_ = false;
        return Status::Success();
    }

    Status Step(bool* has_row) override
    {
        if (!handle_->open)
        {
            return ClosedConnection();
        }
        if (done_)
        {
            return Failure(ErrorCode::Misuse, "Step: the statement is done; Reset it first");
        }
        const UseGuard guard(handle_.Get());
        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW)
        {
            *has_row = true;
            return Status::Success();
        }
        if (rc == SQLITE_DONE)
        {
            *has_row = false;
            done_ = true;
            return Status::Success();
        }
        return StatusFromDb(handle_->db, rc);
    }

    u32 ColumnCount() const override { return static_cast<u32>(sqlite3_column_count(stmt_)); }

    ValueView Column(u32 index) const override
    {
        ENGINE_ASSERT(index < ColumnCount(), "Column: the index is out of range");
        const int i = static_cast<int>(index);
        switch (sqlite3_column_type(stmt_, i))
        {
        case SQLITE_INTEGER: return ValueView::Integer(sqlite3_column_int64(stmt_, i));
        case SQLITE_FLOAT: return ValueView::Real(sqlite3_column_double(stmt_, i));
        case SQLITE_TEXT:
        {
            const auto* text = sqlite3_column_text(stmt_, i);
            const int length = sqlite3_column_bytes(stmt_, i);
            return ValueView::Text(text == nullptr
                                       ? StringView()
                                       : StringView(reinterpret_cast<const char*>(text), static_cast<usize>(length)));
        }
        case SQLITE_BLOB:
        {
            const void* blob = sqlite3_column_blob(stmt_, i);
            const int length = sqlite3_column_bytes(stmt_, i);
            return ValueView::Blob(
                blob == nullptr ? ByteSpan() : ByteSpan(static_cast<const byte*>(blob), static_cast<usize>(length)));
        }
        default: return ValueView::Null();
        }
    }

private:
    SharedPtr<SqliteHandle> handle_;
    sqlite3_stmt* stmt_;
    bool done_ = false;
};

// --- connections
// ---------------------------------------------------------------------------------------------------------------------

constexpr usize kMaxSavepointName = 64;

bool IsSavepointName(StringView name)
{
    if (name.Length() == 0 || name.Length() > kMaxSavepointName)
    {
        return false;
    }
    for (usize i = 0; i < name.Length(); ++i)
    {
        const char c = name.Data()[i];
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        const bool digit = c >= '0' && c <= '9';
        if (!(letter || (digit && i > 0)))
        {
            return false;
        }
    }
    return true;
}

class SqliteConnection final : public Connection
{
public:
    SqliteConnection(SharedPtr<SqliteHandle> handle, bool file, bool wal)
        : handle_(Move(handle)), file_(file), wal_(wal)
    {
    }

    ~SqliteConnection() override
    {
        if (InTransaction())
        {
            static_cast<void>(Execute("ROLLBACK"));  // nowhere to report a failure; SQLite discards the transaction
                                                     // when the connection closes
        }
        handle_->open = false;  // statements that outlive the connection fail with Misuse
        handle_->ClearIdle();
    }

    bool Supports(Feature feature) const override
    {
        switch (feature)
        {
        case Feature::Savepoints:
        case Feature::WithoutRowId:
        case Feature::AddColumn:
        case Feature::NamedParameters:
        case Feature::ForeignKeys: return true;
        case Feature::ConcurrentReaders: return wal_;
        case Feature::WriteAheadLog: return file_;
        }
        return false;
    }

    Status Prepare(SqlText sql, UniquePtr<Statement>* out) override
    {
        const StringView text = sql.View();
        if (text.Length() == 0)
        {
            return Failure(ErrorCode::Syntax, "the statement is empty");
        }
        const UseGuard guard(handle_.Get());
        if (sqlite3_stmt* cached = handle_->TakeIdle(text))
        {
            *out = MakeUnique<SqliteStatement>(handle_, cached);
            return Status::Success();
        }
        sqlite3_stmt* stmt = nullptr;
        const char* tail = nullptr;
        const int rc = sqlite3_prepare_v2(handle_->db, text.Data(), static_cast<int>(text.Length()), &stmt, &tail);
        if (rc != SQLITE_OK)
        {
            const char* message = sqlite3_errmsg(handle_->db);
            return Status::Make(ClassifyPrepareError(rc, message), rc, StringView(message, CStrLength(message)));
        }
        if (stmt == nullptr)
        {
            return Failure(ErrorCode::Syntax, "the statement is empty");
        }
        // What follows the first statement must be nothing but space, semicolons and comments: ask SQLite whether it
        // holds a statement.
        const char* end = text.Data() + text.Length();
        if (tail != nullptr && tail < end)
        {
            sqlite3_stmt* second = nullptr;
            const int more = sqlite3_prepare_v2(handle_->db, tail, static_cast<int>(end - tail), &second, nullptr);
            sqlite3_finalize(second);
            if (more != SQLITE_OK || second != nullptr)
            {
                sqlite3_finalize(stmt);
                return Failure(ErrorCode::Misuse, "Prepare: more than one statement");
            }
        }
        sqlite_detail::NotePreparedStatement();
        *out = MakeUnique<SqliteStatement>(handle_, stmt);
        return Status::Success();
    }

    Status Execute(SqlText sql) override
    {
        UniquePtr<Statement> statement;
        const Status prepared = Prepare(sql, &statement);
        if (prepared.Failed())
        {
            return prepared;
        }
        bool row = true;
        while (row)
        {
            const Status stepped = statement->Step(&row);
            if (stepped.Failed())
            {
                return stepped;
            }
        }
        return Status::Success();
    }

    Status Begin(TransactionKind kind) override
    {
        if (InTransaction())
        {
            return Failure(ErrorCode::Misuse, "Begin: a transaction is already open");
        }
        return kind == TransactionKind::Immediate ? Execute("BEGIN IMMEDIATE") : Execute("BEGIN DEFERRED");
    }

    Status Commit() override
    {
        if (!InTransaction())
        {
            return Failure(ErrorCode::Misuse, "Commit: no transaction is open");
        }
        return Execute("COMMIT");
    }

    Status Rollback() override
    {
        if (!InTransaction())
        {
            return Failure(ErrorCode::Misuse, "Rollback: no transaction is open");
        }
        return Execute("ROLLBACK");
    }

    bool InTransaction() const override { return sqlite3_get_autocommit(handle_->db) == 0; }

    Status Savepoint(StringView name) override { return SavepointStatement("SAVEPOINT", name); }
    Status ReleaseSavepoint(StringView name) override { return SavepointStatement("RELEASE", name); }
    Status RollbackToSavepoint(StringView name) override { return SavepointStatement("ROLLBACK TO", name); }

    i64 LastInsertRowId() const override { return sqlite3_last_insert_rowid(handle_->db); }
    i64 ChangedRows() const override { return sqlite3_changes64(handle_->db); }

    Status SchemaVersion(i64* out) override
    {
        UniquePtr<Statement> statement;
        const Status prepared = Prepare("PRAGMA user_version", &statement);
        if (prepared.Failed())
        {
            return prepared;
        }
        bool row = false;
        const Status stepped = statement->Step(&row);
        if (stepped.Failed())
        {
            return stepped;
        }
        *out = row ? statement->Column(0).AsInteger() : 0;
        return Status::Success();
    }

    Status WriteSchemaVersion(i64 version) override
    {
        char text[64];
        FormatTo(text, sizeof(text), "PRAGMA user_version = %lld", static_cast<long long>(version));
        return Execute(SqlText::Unchecked(StringView(text, CStrLength(text))));
    }

private:
    // The name was checked to be an identifier, so quoting it is enough and the statement is built from nothing else.
    Status SavepointStatement(const char* verb, StringView name)
    {
        if (!IsSavepointName(name))
        {
            return Failure(ErrorCode::Misuse, "the savepoint name must be an identifier of at most 64 characters");
        }
        char text[160];
        FormatTo(text, sizeof(text), "%s \"%.*s\"", verb, static_cast<int>(name.Length()), name.Data());
        const Status status = Execute(SqlText::Unchecked(StringView(text, CStrLength(text))));
        // A name that is not open (or was released with an outer savepoint) comes back as a plain SQLITE_ERROR.
        if (status.Is(ErrorCode::Other))
        {
            return Status::Make(ErrorCode::Misuse, status.BackendCode(), status.Message());
        }
        return status;
    }

    SharedPtr<SqliteHandle> handle_;
    bool file_;
    bool wal_;
};

}  // namespace

const char* SqliteDatabase::Name() const
{
    return "sqlite";
}

bool SqliteDatabase::Supports(Feature feature) const
{
    switch (feature)
    {
    case Feature::Savepoints:
    case Feature::WithoutRowId:
    case Feature::AddColumn:
    case Feature::NamedParameters:
    case Feature::ForeignKeys:
    case Feature::ConcurrentReaders:
    case Feature::WriteAheadLog: return true;
    }
    return false;
}

Status SqliteDatabase::Open(const OpenOptions& options, UniquePtr<Connection>* out)
{
    const bool memory = options.location.Length() == 0 || options.location == StringView(":memory:");
    if (memory && options.mode == OpenMode::ReadOnly)
    {
        return Failure(ErrorCode::Unsupported, "an in-memory database cannot be read-only");
    }
    if (memory && options.journal_mode == JournalMode::WriteAhead)
    {
        return Failure(ErrorCode::Unsupported, "an in-memory database has no write-ahead log");
    }
    char name[1024];
    if (memory)
    {
        CStrCopyTo(name, sizeof(name), ":memory:");
    }
    else
    {
        if (options.location.Length() >= sizeof(name))
        {
            return Failure(ErrorCode::TooBig, "the database location is too long");
        }
        for (usize i = 0; i < options.location.Length(); ++i)
        {
            name[i] = options.location.Data()[i];
        }
        name[options.location.Length()] = '\0';
        if (options.mode != OpenMode::Create && !io::Exists(io::Path(options.location)))
        {
            return Failure(ErrorCode::NotFound, "the database does not exist");
        }
    }
    const int started = sqlite_detail::InitializeSqlite();
    if (started != SQLITE_OK)
    {
        return Status::Make(ErrorCode::Other, started, "SQLite could not start");
    }
    SharedPtr<SqliteHandle> handle = MakeShared<SqliteHandle>();
    if (handle.Get() == nullptr)
    {
        return Failure(ErrorCode::Other, "out of memory");
    }
    const int access = options.mode == OpenMode::ReadOnly ? SQLITE_OPEN_READONLY
                       : options.mode == OpenMode::Create ? (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)
                                                          : SQLITE_OPEN_READWRITE;
    const int rc = sqlite3_open_v2(name, &handle->db, access | SQLITE_OPEN_NOMUTEX, "engine");
    if (rc != SQLITE_OK)
    {
        return StatusFromDb(handle->db, rc);  // the handle closes whatever was allocated
    }
    sqlite3_extended_result_codes(handle->db, 1);
    sqlite3_busy_timeout(handle->db, options.busy_timeout_ms > 0x7fffffffu ? 0x7fffffff
                                                                           : static_cast<int>(options.busy_timeout_ms));

    bool wal = false;
    if (!memory)
    {
        // Opening reads nothing yet: the first statement finds out whether the file is a database at all (a foreign
        // file is NOTADB, `Corrupt`). NORMAL keeps a write-ahead log intact, but SQLite documents that a rollback
        // journal needs FULL to survive a power loss: the default follows the journal mode.
        const bool rollback_journal = options.journal_mode == JournalMode::Delete ||
                                      options.journal_mode == JournalMode::Truncate;
        const char* sync = options.sync_mode == SyncMode::Off      ? "OFF"
                           : options.sync_mode == SyncMode::Full   ? "FULL"
                           : options.sync_mode == SyncMode::Normal ? "NORMAL"
                           : rollback_journal                      ? "FULL"
                                                                   : "NORMAL";
        char pragma[64];
        FormatTo(pragma, sizeof(pragma), "PRAGMA synchronous = %s", sync);
        int step = sqlite3_exec(handle->db, pragma, nullptr, nullptr, nullptr);
        if (step == SQLITE_OK)
        {
            // Asking for the mode with no argument reads it; setting it needs write access, and a database that is
            // already in the write-ahead log keeps it.
            if (options.mode != OpenMode::ReadOnly)
            {
                const bool want_wal = options.journal_mode == JournalMode::Default ||
                                      options.journal_mode == JournalMode::WriteAhead;
                const char* journal = want_wal                                        ? "WAL"
                                      : options.journal_mode == JournalMode::Truncate ? "TRUNCATE"
                                                                                      : "DELETE";
                FormatTo(pragma, sizeof(pragma), "PRAGMA journal_mode = %s", journal);
            }
            else
            {
                FormatTo(pragma, sizeof(pragma), "PRAGMA journal_mode");
            }
            step = sqlite3_exec(handle->db, pragma, NoteJournalMode, &wal, nullptr);
        }
        if (step != SQLITE_OK)
        {
            return StatusFromDb(handle->db, step);
        }
    }
    if (!memory && options.integrity_check != IntegrityCheck::Off)
    {
        bool sound = true;
        const int checked = sqlite3_exec(handle->db,
                                         options.integrity_check == IntegrityCheck::Full ? "PRAGMA integrity_check"
                                                                                         : "PRAGMA quick_check",
                                         NoteIntegrity, &sound, nullptr);
        if (checked != SQLITE_OK)
        {
            return StatusFromDb(handle->db, checked);
        }
        if (!sound)
        {
            return Failure(ErrorCode::Corrupt, "the integrity check found damage");
        }
    }
    handle->cache_capacity = options.statement_cache;
    const int fk = sqlite3_exec(handle->db,
                                options.foreign_keys ? "PRAGMA foreign_keys = ON" : "PRAGMA foreign_keys = OFF",
                                nullptr, nullptr, nullptr);
    if (fk != SQLITE_OK)
    {
        return StatusFromDb(handle->db, fk);
    }
    *out = MakeUnique<SqliteConnection>(Move(handle), !memory, wal);
    return Status::Success();
}

}  // namespace engine_placeholder::database
