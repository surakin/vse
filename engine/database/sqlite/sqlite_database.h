#pragma once

//! [[database::SqliteDatabase]]: the SQLite backend of the database interface.

#include "database/database.h"

namespace engine_placeholder::database {

/// The database interface on SQLite. Built from the vendored amalgamation (`third_party/sqlite`), reached only through this interface.
///
/// An empty `OpenOptions::location` or `":memory:"` opens a private in-memory database that belongs to the one connection that opened it; any other
/// location is a file, read and written through the file layer (`io::File`) by the engine's VFS. A file opens in write-ahead-log mode by default, so
/// readers do not block the writer; `Delete` and `Truncate` journals stay selectable. The locks and the log's index live in this process: **a
/// database file belongs to one process at a time** (a second process is not excluded). Savepoints, `WITHOUT ROWID`, `ALTER TABLE ADD COLUMN`,
/// named parameters and foreign keys are supported; the write-ahead log and concurrent readers need a file.
///
/// Thread-safety: thread-safe. `Open` may be called from any thread; each connection and its statements belong to one thread at a time.
/// Ownership: holds no state; connections do not need the object to outlive them.
class SqliteDatabase final : public Database {
public:
    /// Returns `"sqlite"`.
    /// Ownership: the string is static.
    /// Fails: never.
    const char* Name() const override;
    /// Returns true for every feature: the backend can do them for a file database. A connection's own `Supports` is exact (an in-memory database has no
    /// write-ahead log).
    ///
    /// Fails: never.
    bool Supports(Feature feature) const override;
    /// Opens an in-memory database or a file.
    ///
    /// Param options: `location` empty or `":memory:"` (private, memory), or a path; `mode` `Create` makes a missing file, the others need it to exist;
    /// an in-memory database cannot be `ReadOnly` or `WriteAhead`; `journal_mode` `Default` is the write-ahead log for a file; `sync_mode` `Default` is
    /// `Normal`; `busy_timeout_ms` and `foreign_keys` apply.
    /// Param out: receives the connection on success; unchanged on failure.
    /// Ownership: `options.location` is read during the call only; the connection owns its database and does not need this object.
    /// Fails: `Unsupported` (in memory: read-only or write-ahead log); `NotFound` (a missing file without `Create`); `Corrupt` (the file is not a database); `Io`; `Busy`; `TooBig` (location longer than 1,023 bytes); `Other` if SQLite cannot start.
    Status Open(const OpenOptions& options, UniquePtr<Connection>* out) override;
};

} // namespace engine_placeholder::database
