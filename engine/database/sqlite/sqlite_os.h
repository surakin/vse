#pragma once

//! The SQLite library's start-up: its mutex methods and its virtual file system (internal to `engine/database/sqlite/`, not a public API).

namespace engine_placeholder::database::sqlite_detail {

/// Installs the engine's mutex methods, then initializes SQLite, which registers the engine's VFS (`sqlite3_os_init`). Runs once; every later call
/// returns the first call's result. Every translation unit that calls SQLite calls this first, which also keeps the object that defines `sqlite3_os_init`
/// in the link (a static library is searched once, in link order).
///
/// Thread-safety: thread-safe.
/// Returns: 0 (`SQLITE_OK`) or the SQLite result code of the failure.
int InitializeSqlite();

} // namespace engine_placeholder::database::sqlite_detail
