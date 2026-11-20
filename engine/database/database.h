#pragma once

//! The database interface (Phase 19 Day 252): [[database::Database]] opens a [[database::Connection]], a connection
//! prepares [[database::Statement]]s and runs transactions. Upper layers never know the backend: SQLite and the
//! in-memory database implement the same interface and pass one conformance suite. SQL is [[database::SqlText]] (a
//! literal) and data is bound, never formatted into the text. Errors are [[database::Status]] values.

#include "containers/span.h"
#include "containers/unique_ptr.h"
#include "database/sql_text.h"
#include "database/status.h"
#include "database/value.h"
#include "string_view.h"
#include "types.h"

namespace vse::database
{

/// A capability a backend may lack. Code asks [[database::Connection::Supports]] before relying on one; using a missing
/// feature returns
/// [[database::ErrorCode::Unsupported]] or [[database::ErrorCode::Syntax]].
enum class Feature : u8
{
    Savepoints,         ///< `Savepoint`, `ReleaseSavepoint` and `RollbackToSavepoint`.
    WithoutRowId,       ///< `CREATE TABLE ... WITHOUT ROWID`.
    AddColumn,          ///< `ALTER TABLE ... ADD COLUMN`.
    NamedParameters,    ///< `:name` parameters and `Statement::ParameterIndex`.
    ConcurrentReaders,  ///< Reads on other connections proceed while one connection writes.
    WriteAheadLog,      ///< `JournalMode::WriteAhead` is honoured.
    ForeignKeys,        ///< `REFERENCES` rules are enforced.
};

/// Returns a stable lower-case name for `feature` (`"savepoints"`); never null.
///
/// - Fails: never.
/// - Ownership: the returned string is static.
const char* FeatureName(Feature feature);

/// How a connection opens its database.
enum class OpenMode : u8
{
    ReadWrite,  ///< Read and write; the database must exist.
    ReadOnly,   ///< Read only; the database must exist.
    Create,     ///< Read and write; the database is created when it does not exist.
};

/// How a backend makes commits durable and atomic. `Default` leaves the backend's own choice.
enum class JournalMode : u8
{
    Default,     ///< The backend's default.
    Delete,      ///< A rollback journal, deleted at commit.
    Truncate,    ///< A rollback journal, truncated at commit.
    WriteAhead,  ///< A write-ahead log (readers do not block the writer); needs [[database::Feature::WriteAheadLog]].
};

/// How hard a commit is pushed to the storage device. `Default` leaves the backend's own choice.
enum class SyncMode : u8
{
    Default,  ///< The backend's default (the SQLite backend: `Normal` with the write-ahead log, `Full` with a rollback
              ///< journal).
    Off,      ///< No syncs: a crash can lose or damage the database.
    Normal,   ///< Sync at the points that keep the database intact; the latest commits can be lost in a crash.
    Full,     ///< Sync at every commit.
};

/// A check of the stored database made when a file is opened. `Off` skips it.
enum class IntegrityCheck : u8
{
    Off,    ///< No check.
    Quick,  ///< A fast check of the structure (no index-against-table comparison); a failure opens as
            ///< [[database::ErrorCode::Corrupt]].
    Full,   ///< The complete check, reading the whole file; a failure opens as [[database::ErrorCode::Corrupt]].
};

/// Everything `Database::Open` needs, as plain fields.
///
/// - Thread-safety: thread-compatible plain data.
/// - Ownership: `location` borrows its text; the caller keeps it alive for the `Open` call.
struct OpenOptions
{
    /// The database to open: a path in the engine's portable form, or `":memory:"` for an in-memory database. An empty
    /// location is a private database that lives as long as its connection; so is `":memory:"` (the SQLite backend has
    /// no shared named in-memory database, and any other text is a file path).
    StringView location;
    OpenMode mode = OpenMode::Create;                 ///< Read-write, read-only or create.
    u32 busy_timeout_ms = 5000;                       ///< How long a lock wait lasts before the operation returns
                                                      ///< [[database::ErrorCode::Busy]]; 0 fails at once.
    JournalMode journal_mode = JournalMode::Default;  ///< The journal mode.
    SyncMode sync_mode = SyncMode::Default;           ///< The sync mode.
    bool foreign_keys = true;  ///< Enforce `REFERENCES` rules (when [[database::Feature::ForeignKeys]] is supported).
    IntegrityCheck
        integrity_check = IntegrityCheck::Off;  ///< A check made while opening a file (not an in-memory database).
    u32 statement_cache = 32;  ///< How many finished statements the connection keeps for reuse by `Prepare` of the same
                               ///< text; 0 keeps none.
};

/// How a transaction takes its locks.
enum class TransactionKind : u8
{
    Deferred,   ///< Locks are taken when the first statement needs them (a write can then return `Busy`).
    Immediate,  ///< The write lock is taken at `Begin`: the transaction either starts as the one writer or returns
                ///< `Busy` at once.
};

/// A prepared statement: bind parameters, `Step` through the rows, read the columns.
///
/// Parameters are numbered from 1, columns from 0. After `Step` returns a row, the column getters read it; a text or
/// blob view stays valid until the next `Step`, `Reset` or destruction. Bind before the first `Step` or after `Reset`.
///
/// - Thread-safety: not-thread-safe; one thread at a time, and the thread that uses the connection.
/// - Ownership: made by [[database::Connection::Prepare]]; it must be destroyed before its connection.
class Statement
{
public:
    virtual ~Statement() = default;

    /// The number of parameters (the highest parameter number).
    /// - Fails: never.
    virtual u32 ParameterCount() const = 0;
    /// The number of the parameter named `name` including its prefix (`":id"`), for binding by name.
    ///
    /// - Param name: the parameter as written in the SQL, with its `:` prefix.
    /// - Param index: receives the number (from 1).
    /// - Fails: `NotFound` when the statement has no such parameter; `Unsupported` without
    /// [[database::Feature::NamedParameters]].
    /// - Ownership: `index` is written during the call only.
    virtual Status ParameterIndex(StringView name, u32* index) const = 0;

    /// Binds `value` to parameter `index` (from 1), replacing an earlier binding. Text and blob contents are copied
    /// before the call returns.
    ///
    /// - Fails: `Misuse` when `index` is 0 or above [[database::Statement::ParameterCount]]; `TooBig` when the value
    /// exceeds the backend's limit.
    virtual Status Bind(u32 index, const ValueView& value) = 0;
    /// Binds null to every parameter.
    /// - Fails: never for a healthy statement.
    virtual Status ClearBindings() = 0;
    /// Rewinds the statement so `Step` starts again; bindings are kept. Also releases the locks a read statement holds.
    ///
    /// - Fails: never; the error of a failed `Step` was reported by that `Step`.
    virtual Status Reset() = 0;

    /// Runs the statement to its next row or to its end.
    ///
    /// - Param has_row: receives true when a row is ready to read, false when the statement is done.
    /// - Fails: `Constraint`, `Busy`, `Locked`, `ReadOnly`, `Full`, `Io`, `Corrupt` and the others of
    /// [[database::ErrorCode]]; a failed write statement changed nothing; `Misuse` for a `Step` after the statement
    /// reported done and before `Reset`.
    /// - Ownership: `has_row` is written during the call only; a row's column views stay valid until the next `Step` or
    /// `Reset`.
    virtual Status Step(bool* has_row) = 0;

    /// The number of columns of the result (0 for a statement that returns no rows).
    /// - Fails: never.
    virtual u32 ColumnCount() const = 0;
    /// The value of column `index` of the current row, as the type it was stored with. Text and blob views are valid
    /// until the next `Step` or `Reset`.
    ///
    /// - Param index: the column, from 0.
    /// - Aborts: `index` is out of range.
    /// - Fails: with no current row (before the first `Step`, or after one that reported done) the value is null.
    virtual ValueView Column(u32 index) const = 0;
};

/// One connection to a database: statements, transactions and the facts about the last write.
///
/// A connection is in one of two states, outside or inside a transaction. A statement outside a transaction commits on
/// its own.
///
/// - Thread-safety: not-thread-safe; one thread at a time (a connection may move between threads between calls).
/// Separate connections to one database are safe to use from separate threads; writes are serialized by the backend and
/// a writer that waits longer than the busy timeout gets `Busy`.
/// - Ownership: made by [[database::Database::Open]]; closing it (destruction) rolls back an open transaction.
class Connection
{
public:
    virtual ~Connection() = default;

    /// Returns true when the backend supports `feature`.
    /// - Fails: never.
    virtual bool Supports(Feature feature) const = 0;

    /// Compiles `sql` into a statement.
    ///
    /// - Param sql: one statement; a literal, so no data is formatted into it.
    /// - Param out: receives the statement on success; unchanged on failure.
    /// - Fails: `Syntax` for text the backend cannot parse; `Unsupported` for a form it lacks; `Misuse` for more than
    /// one statement.
    /// - Ownership: a statement used after its connection was destroyed fails with `Misuse`; destroy statements first.
    virtual Status Prepare(SqlText sql, UniquePtr<Statement>* out) = 0;
    /// Runs one statement that has no parameters and returns no rows that matter (schema changes, transaction control,
    /// pragmas of a backend).
    ///
    /// - Fails: as `Prepare` and `Step`.
    virtual Status Execute(SqlText sql) = 0;

    /// Starts a transaction.
    ///
    /// - Fails: `Misuse` inside a transaction (they do not nest; use savepoints); `Busy` for `Immediate` when another
    /// writer holds the lock.
    virtual Status Begin(TransactionKind kind) = 0;
    /// Makes the transaction's changes durable and ends it.
    /// - Fails: `Misuse` outside a transaction; `Busy`, `Io`, `Full`, `Constraint` (a deferred foreign key); on failure
    /// the transaction is still open.
    virtual Status Commit() = 0;
    /// Undoes the transaction's changes and ends it.
    /// - Fails: `Misuse` outside a transaction.
    virtual Status Rollback() = 0;
    /// Returns true inside a transaction.
    /// - Fails: never.
    virtual bool InTransaction() const = 0;

    /// Marks a point inside the transaction (starting one if none is open) that `RollbackToSavepoint` can return to.
    /// Savepoints nest.
    ///
    /// - Param name: an identifier (a letter or underscore, then letters, digits and underscores).
    /// - Fails: `Unsupported` without [[database::Feature::Savepoints]]; `Misuse` for a name that is not an identifier.
    virtual Status Savepoint(StringView name) = 0;
    /// Forgets the savepoint `name` and those inside it, keeping their changes.
    /// - Fails: `Misuse` when no such savepoint is open; `Unsupported` without [[database::Feature::Savepoints]].
    virtual Status ReleaseSavepoint(StringView name) = 0;
    /// Undoes the changes made since the savepoint `name`; the savepoint stays open.
    /// - Fails: `Misuse` when no such savepoint is open; `Unsupported` without [[database::Feature::Savepoints]].
    virtual Status RollbackToSavepoint(StringView name) = 0;

    /// The key of the row most recently inserted on this connection into a table with an `INTEGER PRIMARY KEY` (0 when
    /// none was).
    /// - Fails: never.
    virtual i64 LastInsertRowId() const = 0;
    /// The number of rows the most recent `INSERT`, `UPDATE` or `DELETE` on this connection changed.
    /// - Fails: never.
    virtual i64 ChangedRows() const = 0;

    /// Reads the schema version, a number the application owns (0 in a new database), so no backend-specific statement
    /// is needed to track migrations.
    /// - Fails: `Busy`, `Io`, `Corrupt`.
    /// - Ownership: `out` is written during the call only.
    virtual Status SchemaVersion(i64* out) = 0;
    /// Writes the schema version; inside a transaction it commits with it.
    /// - Fails: `ReadOnly`, `Busy`, `Io`.
    virtual Status WriteSchemaVersion(i64 version) = 0;
};

/// A backend: opens connections to its databases.
///
/// - Thread-safety: thread-safe. `Open` may be called from any thread; the connections it made follow
/// [[database::Connection]]'s rules.
/// - Ownership: the database object must outlive the connections it opened.
class Database
{
public:
    virtual ~Database() = default;

    /// A short stable name for logs and tests (`"memory"`, `"sqlite"`); never null.
    /// - Fails: never.
    /// - Ownership: the returned string is static.
    virtual const char* Name() const = 0;
    /// Returns true when the backend supports `feature` (what the backend can do for some database; a connection's own
    /// `Supports` is exact for its database).
    /// - Fails: never.
    virtual bool Supports(Feature feature) const = 0;
    /// Opens a connection.
    ///
    /// - Param options: what to open and how.
    /// - Param out: receives the connection on success; unchanged on failure.
    /// - Fails: `NotFound` for a missing database with a mode other than `Create`; `Corrupt`, `Io`, `Busy`, `ReadOnly`,
    /// `Unsupported` (a journal mode the backend lacks).
    /// - Ownership: `options.location` is read during the call only; the connection must be destroyed before the
    /// database.
    virtual Status Open(const OpenOptions& options, UniquePtr<Connection>* out) = 0;
};

/// A transaction that rolls back unless committed: begin at construction, `Commit` to keep the changes, destruction
/// rolls back.
///
/// - Thread-safety: not-thread-safe; the thread that uses the connection.
/// - Ownership: borrows the connection, which must outlive the guard.
class Transaction
{
public:
    /// Begins a transaction on `connection`; check [[database::Transaction::BeginStatus]].
    Transaction(Connection& connection, TransactionKind kind = TransactionKind::Immediate);
    /// Rolls the transaction back when it was begun and not committed.
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    /// The result of the `Begin` made by the constructor.
    /// - Fails: never.
    const Status& BeginStatus() const { return begin_; }
    /// Commits. After a successful commit the destructor does nothing; after a failed one the transaction is still open
    /// and the destructor rolls it back.
    ///
    /// - Fails: the connection's `Commit` failure; `Misuse` when `Begin` failed or the transaction was already
    /// committed.
    Status Commit();

private:
    Connection& connection_;
    Status begin_;
    bool open_ = false;
};

}  // namespace vse::database
