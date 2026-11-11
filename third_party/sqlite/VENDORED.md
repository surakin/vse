# Vendored SQLite

Upstream: https://sqlite.org (the amalgamation, `sqlite-amalgamation-3540100.zip`, 2,990,966 bytes)
Pinned version: **3.54.1** (source id `2026-10-16 14:45:20 555e07ef33f69a0957d24d8e555b702464a131d6f1607f4c3c1e4551e8e6ca76`).
Downloaded 2026-11-11 from `https://sqlite.org/2026/sqlite-amalgamation-3540100.zip`;
SHA3-256 `92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30` (the value sqlite.org publishes on its download page),
SHA-256 `0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9`.
License: public domain; the blessing in the header comment of `sqlite3.h` is kept in `LICENSE`.

Per `PLAN.md` assumption #25 and `docs/superpowers/specs/2026-11-11-storage-design.md` this is a source vendor (not a submodule, not
`FetchContent`) so builds stay offline and reproducible. It is used only behind `engine/database/sqlite/`; the gate
`sqlite_boundary_check` lets no other file include `sqlite3.h`.

## Kept from the upstream archive

`sqlite3.c` and `sqlite3.h`, unmodified. Not kept: `shell.c` (no shell is built) and `sqlite3ext.h` (no extension loading).

## How it is built (`CMakeLists.txt`)

One static library, `sqlite3`, built once with plain flags and linked into the plain, ASan and TSan flavors alike (so a sanitizer does not look inside
SQLite). Compile definitions:

| Definition | Why |
|---|---|
| `SQLITE_OS_OTHER=1` | No built-in unix or Windows OS layer: the engine's VFS over `io::File` (Phase 19 Day 254) is the only one, and `sqlite3_os_init` is ours. |
| `SQLITE_THREADSAFE=2` | Multi-thread mode: a connection is used by one thread at a time. The mutex methods are installed by the engine (`sqlite3_config(SQLITE_CONFIG_MUTEX)`, Day 254) on its own `Mutex`, because `SQLITE_OS_OTHER` has no mutex implementation. |
| `SQLITE_OMIT_DESERIALIZE` | Drops the `memdb` VFS, whose initialisation needs a default VFS to exist already (without it `sqlite3_initialize` fails until a VFS is registered); the engine does not serialize databases. |
| `SQLITE_OMIT_LOAD_EXTENSION` | No extension loading. |
| `SQLITE_OMIT_DEPRECATED` | No deprecated interfaces. |
| `SQLITE_OMIT_SHARED_CACHE` | No shared-cache mode (a connection is private). |
| `SQLITE_DQS=0` | A double-quoted string is an identifier, never a string literal. |
| `SQLITE_DEFAULT_FOREIGN_KEYS=1` | Foreign keys on by default. |
| `SQLITE_DEFAULT_MEMSTATUS=0` | No global memory statistics (they take a mutex on every allocation). |
| `SQLITE_TEMP_STORE=3` | Temporary tables and sort spills stay in memory: the VFS of an in-memory database opens no temporary files. |

The vendored target keeps its own warning flags (the engine's `-Werror` is added after `third_party/`), is excluded from unity builds, and is not
instrumented by the sanitizers.
