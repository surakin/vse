#pragma once

//! [[database::SqliteDatabase]]: the SQLite backend of the database interface.

#include "database/database.h"

namespace engine_placeholder::database {

/// The database interface on SQLite. Built from the vendored amalgamation (`third_party/sqlite`), reached only through this interface.
///
/// Day 253 opens private in-memory databases (`OpenOptions::location` empty or `":memory:"`); a file location is `Unsupported` until the file-layer VFS
/// arrives (Day 254). Each in-memory database belongs to the one connection that opened it. Savepoints, `WITHOUT ROWID`, `ALTER TABLE ADD COLUMN`,
/// named parameters and foreign keys are supported; the write-ahead log and concurrent readers need a file and are not (yet).
///
/// Thread-safety: thread-safe. `Open` may be called from any thread; each connection and its statements belong to one thread at a time.
/// Ownership: holds no state; connections do not need the object to outlive them.
class SqliteDatabase final : public Database {
public:
    /// Returns `"sqlite"`.
    /// Ownership: the string is static.
    /// Fails: never.
    const char* Name() const override;
    /// Returns true for savepoints, `WITHOUT ROWID`, add-column, named parameters and foreign keys.
    /// Fails: never.
    bool Supports(Feature feature) const override;
    /// Opens a private in-memory database.
    ///
    /// Param options: `location` empty or `":memory:"`; `mode` other than `ReadOnly`; `journal_mode` other than `WriteAhead`; `busy_timeout_ms` and `foreign_keys` apply.
    /// Param out: receives the connection on success; unchanged on failure.
    /// Ownership: `options.location` is read during the call only; the connection owns its database and does not need this object.
    /// Fails: `Unsupported` for a file location, a read-only mode or the write-ahead log; `Other` if SQLite cannot start or open.
    Status Open(const OpenOptions& options, UniquePtr<Connection>* out) override;
};

} // namespace engine_placeholder::database
