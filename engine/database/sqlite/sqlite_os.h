#pragma once

//! The SQLite library's start-up: its mutex methods and its virtual file system (internal to `engine/database/sqlite/`,
//! not a public API).

namespace engine_placeholder::database::sqlite_detail
{

/// Installs the engine's mutex methods, then initializes SQLite, which registers the engine's VFS (`sqlite3_os_init`).
/// Runs once; every later call returns the first call's result. Every translation unit that calls SQLite calls this
/// first, which also keeps the object that defines `sqlite3_os_init` in the link (a static library is searched once, in
/// link order).
///
/// - Thread-safety: thread-safe.
/// - Returns: 0 (`SQLITE_OK`) or the SQLite result code of the failure.
int InitializeSqlite();

/// Counts one statement compiled by SQLite (not one served from a connection's statement cache). Called by the backend.
///
/// - Thread-safety: thread-safe.
void NotePreparedStatement();

/// The number of statements SQLite has compiled in this process since start: a test reads it before and after a loop to
/// see whether the statement cache served the repeats.
///
/// - Thread-safety: thread-safe.
unsigned long long PreparedStatementCount();

/// The file operations a test can make fail ([[database::sqlite_detail::SetFileFaults]]), combined with `|`.
enum FileFaultOp : unsigned
{
    kFaultWrite = 1,     ///< A write to a file.
    kFaultSync = 2,      ///< A sync of a file (and of its directory).
    kFaultTruncate = 4,  ///< A truncation.
};

/// Makes the `fail_at`-th (counting from 0) operation of the kinds in `ops`, from now on, fail with an I/O error: a
/// crash test's fault injector. With `crash` set, that operation and every file operation after it fails too (a write,
/// sync, truncation, deletion or creation), so nothing more reaches the disk, as after a power loss. Replaces an
/// earlier plan and resets [[database::sqlite_detail::FileFaultOpsSeen]]. For tests only.
///
/// - Thread-safety: thread-safe.
void SetFileFaults(unsigned ops, long long fail_at, bool crash);

/// Removes the fault plan.
///
/// - Thread-safety: thread-safe.
void ClearFileFaults();

/// The number of operations of the planned kinds seen since [[database::sqlite_detail::SetFileFaults]]: run a workload
/// with a `fail_at` it never reaches to learn how many operations it makes.
///
/// - Thread-safety: thread-safe.
long long FileFaultOpsSeen();

}  // namespace engine_placeholder::database::sqlite_detail
