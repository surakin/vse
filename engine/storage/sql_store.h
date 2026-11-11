#pragma once

//! [[storage::SqlStore]] (Phase 19 Day 262): the record store on a SQL database, through the database interface only.

#include "database/database.h"
#include "storage/component_registry.h"
#include "storage/store.h"
#include "sync/mutex.h"
#include "sync/thread.h"

namespace engine_placeholder::storage {

namespace detail {
struct SqlLayout;
} // namespace detail

/// How a field value is compared in [[storage::SqlStore::FindKeys]].
enum class CompareOp : u8 {
    Equal,          ///< Equal to the value.
    NotEqual,       ///< Not equal to the value.
    Less,           ///< Less than the value.
    LessOrEqual,    ///< Less than or equal to the value.
    Greater,        ///< Greater than the value.
    GreaterOrEqual, ///< Greater than or equal to the value.
};

/// A column to index in the column layout, named as `FindKeys` names it.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: borrows its names (string literals).
struct IndexRequest {
    const char* component = ""; ///< The registered name of a queryable component.
    const char* column = "";    ///< A column of its table (a field name, or `<field>_x` for a vector part).
};

/// What a [[storage::SqlStore]] does beyond keeping blobs.
///
/// Thread-safety: thread-compatible plain data.
/// Ownership: `registry` is borrowed and must outlive the store.
struct SqlStoreOptions {
    /// The registry that names the queryable components; needed for `columns`.
    const ComponentRegistry* registry = nullptr;
    /// Keep a table `c_<name>` for every component flagged `kComponentQueryable`, written alongside the entity row and used by `FindKeys`.
    bool columns = false;
    /// The bounds used to decode the blobs for the column tables.
    CodecLimits limits;
    /// Columns to index (`CREATE INDEX IF NOT EXISTS`) when `columns` is set; an unknown component or column makes `Open` return `Invalid`.
    const IndexRequest* indexes = nullptr;
    /// How many requests `indexes` holds.
    usize index_count = 0;
};

/// The record store on a [[database::Connection]]: one table holds the records, one the metadata, and, in column layout, one table per queryable component.
///
/// - `entity(key BLOB PRIMARY KEY, seq INTEGER NOT NULL, hash BLOB NOT NULL, data BLOB NOT NULL)`: the key as 16 bytes big-endian (so the database's order is
///   key order), the number of the write that stored it, the content hash as 8 bytes big-endian, and the record's bytes;
/// - `meta(key TEXT PRIMARY KEY, value BLOB)`.
///
/// (A database migrated in place by `ALTER TABLE ... ADD COLUMN` has `seq` without `NOT NULL`, the rebuild path has it with; the store does not depend on the difference.)
///
/// **Column layout.** With `SqlStoreOptions::columns`, a component flagged `kComponentQueryable` also gets `c_<name>(key BLOB PRIMARY KEY REFERENCES entity(key) ON DELETE CASCADE,
/// one column per scalar or vector field)`: integers, booleans and enums are `INTEGER` (a `u64` as its bit pattern), `f32`/`f64` are `REAL` (a NaN is stored as NULL), a
/// `Vec2`/`Vec3`/`Vec4`/`Quaternion` is one `REAL` column per part (`<field>_x`, `_y`, `_z`, `_w`); strings, arrays, matrices, boxes and references stay in the blob only. The blob
/// remains the source of truth: a load reads it alone, so both layouts load the same world. The tables follow the registry: a new field is added with `ALTER TABLE ADD COLUMN`
/// where the connection supports it, otherwise the table is rebuilt, and in both cases refilled from the blobs inside the transaction `Open` makes. Opening a database that was
/// last written without columns refills every table from the blobs. Table and column names come from the registry and must be plain identifiers (`Open` returns `Invalid`
/// otherwise); they are the only SQL text not written as a literal, and all values are bound.
///
/// Only the portable statement subset of the storage design (section 4c) is used, every statement is prepared once when the store opens and data is only bound.
/// The schema version is the connection's (`Connection::SchemaVersion`) and is also written to the metadata (`schema_version`); `Open` applies the missing
/// migration steps in one transaction, and a database with a newer schema than this program knows is refused with `TooNew` and left as it was.
///
/// A store borrows its connection and uses it alone: while a batch is open, every call from another thread returns `Busy` (one connection cannot show
/// committed state to readers while it holds a transaction); without a batch any thread may call, one at a time.
///
/// Thread-safety: thread-safe; one internal lock around the connection.
/// Ownership: borrows the connection, which must outlive the store; owns its prepared statements.
class SqlStore final : public Store {
public:
    /// The schema version this program writes and reads.
    static constexpr i64 kSchemaVersion = 3;

    /// Opens the store on `connection`: creates or migrates the schema and prepares the statements.
    ///
    /// Param connection: an open read-write connection with no transaction open.
    /// Param out: receives the store on success; unchanged on failure.
    /// Ownership: `connection` is borrowed and must outlive the store; `out` owns the store.
    /// Fails: `TooNew` (the database has a newer schema; nothing changed); `Corrupt`, `Busy`, `Io`, `Invalid` (a transaction is open); the database's own failures mapped to these.
    static Status Open(database::Connection& connection, UniquePtr<SqlStore>* out);
    /// As the other `Open`, with the layout choices of `options`.
    ///
    /// Param options: the registry and whether to keep column tables.
    /// Ownership: `connection` and `options.registry` are borrowed and must outlive the store.
    /// Fails: as the other `Open`; `Invalid` (columns without a registry, a queryable component or field whose name is not a plain identifier).
    static Status Open(database::Connection& connection, UniquePtr<SqlStore>* out, const SqlStoreOptions& options);

    /// Borrows `connection` without touching it; use `SqlStore::Open`, which makes the schema and prepares the statements (the constructor is public only so that `MakeUnique` can reach it).
    ///
    /// Param connection: an open connection.
    /// Ownership: borrowed; must outlive the store.
    explicit SqlStore(database::Connection& connection);
    /// Rolls back an open batch.
    ~SqlStore() override;
    SqlStore(const SqlStore&) = delete;
    SqlStore& operator=(const SqlStore&) = delete;

    /// Returns `"sql"`.
    /// Ownership: the string is static.
    /// Fails: never.
    const char* Name() const override;
    Status Put(const EntityRecord& record) override;
    /// Writes the records with multi-row statements (sixteen rows at a time), then their column rows.
    Status PutMany(Span<const EntityRecord> records) override;
    Status Get(EntityKey key, EntityRecord* out) const override;
    Status Remove(EntityKey key) override;
    Status Scan(const KeyRange& range, RecordVisitor& visitor) const override;
    Status Count(usize* out) const override;
    Status Begin() override;
    Status Commit() override;
    Status Rollback() override;
    Status GetMetadata(StringView name, String<HeapAllocator>* out) const override;
    Status SetMetadata(StringView name, StringView value) override;

    /// Returns the keys of the entities whose `component` has a `column` that compares true with `value`, in key order. Answers from the column table; no blob is read.
    ///
    /// Param component: the registered name of a queryable component.
    /// Param column: a column of its table (a field name, or `<field>_x` for a vector part).
    /// Param op: the comparison, `column op value`.
    /// Param value: an integer for an integer column, an integer or a real for a real one.
    /// Param out: receives the keys (cleared first).
    /// Ownership: all borrowed for the call.
    /// Fails: `Unsupported` (the store has no column layout, or the component is not queryable); `NotFound` (no such component or column); `Invalid` (the value has the wrong type); `Busy`; `Io`; `Corrupt`.
    Status FindKeys(StringView component, StringView column, CompareOp op, const database::ValueView& value, Vector<EntityKey>* out) const;
    /// Counts the entities `FindKeys` would return.
    ///
    /// Param out: receives the count.
    /// Ownership: all borrowed for the call.
    /// Fails: as `FindKeys`.
    Status CountWhere(StringView component, StringView column, CompareOp op, const database::ValueView& value, usize* out) const;
    /// Checks the column tables against the blobs: every queryable component of every entity has exactly the row its blob gives, and no row is left over. For tests and tools.
    ///
    /// Param mismatches: receives the number of rows that are missing, extra or different.
    /// Ownership: `mismatches` is written during the call only.
    /// Fails: `Unsupported` (no column layout); `Busy`; `Io`; `Corrupt`.
    Status VerifyColumns(usize* mismatches) const;

    /// Makes the `n`th next mutation, `Begin` or `Commit` fail with `Io` (1 is the next); that one only. 0 clears a pending injection. For tests.
    ///
    /// Param n: the operation number.
    /// Fails: never.
    void FailAfter(u32 n);

private:
    struct Statements;
    Status SetupLayout(const SqlStoreOptions& options);
    Status WriteColumns(const EntityRecord& record) const;
    Status RemoveColumns(EntityKey key) const;
    Status Query(StringView component, StringView column, CompareOp op, const database::ValueView& value, Vector<EntityKey>* keys, usize* count) const;
    Status Prepare();
    Status Guard() const;
    bool ShouldFail();

    database::Connection* connection_;
    mutable Mutex lock_;
    bool in_batch_ = false;
    ThreadId owner_;
    u32 fail_in_ = 0;
    i64 next_seq_ = 1;
    UniquePtr<Statements> statements_;
    UniquePtr<detail::SqlLayout> layout_;
    CodecLimits limits_;
    const ComponentRegistry* registry_ = nullptr;
};

} // namespace engine_placeholder::storage
