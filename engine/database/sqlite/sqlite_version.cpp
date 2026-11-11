#include "database/sqlite/sqlite_version.h"

#include <sqlite3.h>

namespace engine_placeholder::database {

const char* SqliteVersion() { return sqlite3_libversion(); }

} // namespace engine_placeholder::database

// SQLITE_OS_OTHER (third_party/sqlite/VENDORED.md): SQLite has no operating-system layer of its own, and calls these two functions from
// sqlite3_initialize and sqlite3_shutdown. Day 251 stub: no VFS is registered yet, so nothing can be opened; Day 254 replaces them with the VFS
// over io::File and the mutex methods. They stay in the same file as the code that first calls into SQLite on purpose: a static library is
// searched once, in link order, so the object that needs sqlite3.c must also carry the two functions sqlite3.c asks for.
extern "C" int sqlite3_os_init() { return SQLITE_OK; }
extern "C" int sqlite3_os_end() { return SQLITE_OK; }
