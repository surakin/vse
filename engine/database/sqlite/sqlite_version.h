#pragma once

//! The SQLite backend of the database interface (Phase 19). Nothing in this directory's public headers names a SQLite
//! type; the implementation files here are the only ones that include `sqlite3.h` (`tools/sqlite_boundary_check`).

namespace vse::database
{

/// The version of the vendored SQLite library, as its own text (for example `3.54.1`).
///
/// - Returns: a NUL-terminated string with static lifetime.
/// - Ownership: the string is static.
/// - Fails: never.
const char* SqliteVersion();

}  // namespace vse::database
