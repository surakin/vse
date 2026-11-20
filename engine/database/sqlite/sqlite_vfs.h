#pragma once

//! The engine's SQLite virtual file system over the file layer (internal to `engine/database/sqlite/`).

#include <sqlite3.h>

namespace vse::database::sqlite_detail
{

/// The VFS named `"engine"`: files through `io::File`, locks and the write-ahead-log index in process memory.
///
/// - Thread-safety: thread-safe.
/// - Ownership: the VFS is static and lives for the whole program.
/// - Fails: never.
sqlite3_vfs* EngineVfs();

}  // namespace vse::database::sqlite_detail
