#pragma once

//! [[storage::SqlStore]] (Phase 19 Day 262): the record store on a SQL database, through the database interface only.

#include "database/database.h"
#include "storage/store.h"
#include "sync/mutex.h"
#include "sync/thread.h"

namespace engine_placeholder::storage {

/// The record store on a [[database::Connection]]: one table holds the records, one the metadata.
///
/// - `entity(key BLOB PRIMARY KEY, seq INTEGER NOT NULL, hash BLOB NOT NULL, data BLOB NOT NULL)`: the key as 16 bytes big-endian (so the database's order is
///   key order), the number of the write that stored it, the content hash as 8 bytes big-endian, and the record's bytes;
/// - `meta(key TEXT PRIMARY KEY, value BLOB)`.
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
    static constexpr i64 kSchemaVersion = 2;

    /// Opens the store on `connection`: creates or migrates the schema and prepares the statements.
    ///
    /// Param connection: an open read-write connection with no transaction open.
    /// Param out: receives the store on success; unchanged on failure.
    /// Ownership: `connection` is borrowed and must outlive the store; `out` owns the store.
    /// Fails: `TooNew` (the database has a newer schema; nothing changed); `Corrupt`, `Busy`, `Io`, `Invalid` (a transaction is open); the database's own failures mapped to these.
    static Status Open(database::Connection& connection, UniquePtr<SqlStore>* out);

    /// Borrows `connection` without touching it; use [[storage::SqlStore::Open]], which makes the schema and prepares the statements (the constructor is public only so that `MakeUnique` can reach it).
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
    Status Get(EntityKey key, EntityRecord* out) const override;
    Status Remove(EntityKey key) override;
    Status Scan(const KeyRange& range, RecordVisitor& visitor) const override;
    Status Count(usize* out) const override;
    Status Begin() override;
    Status Commit() override;
    Status Rollback() override;
    Status GetMetadata(StringView name, String<HeapAllocator>* out) const override;
    Status SetMetadata(StringView name, StringView value) override;

    /// Makes the `n`th next mutation, `Begin` or `Commit` fail with `Io` (1 is the next); that one only. 0 clears a pending injection. For tests.
    ///
    /// Param n: the operation number.
    /// Fails: never.
    void FailAfter(u32 n);

private:
    struct Statements;
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
};

} // namespace engine_placeholder::storage
