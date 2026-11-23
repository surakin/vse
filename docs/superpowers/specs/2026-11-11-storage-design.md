# Storage and database design (Phase 19)

Written 2026-11-11 (Phase 19 Day 250). Plan: `plans/phase-19-storage.md`; `PLAN.md` assumption #25. The decisions below are the planner's defaults, kept or amended by the four spikes of section 3 (throwaway programs, system SQLite 3.54.1, `g++ -O2`, Ryzen 9 7950X3D, NVMe, `synchronous=NORMAL` unless stated).

## 1. Intent

Save games, persistent worlds, editor scenes, server state and tools all need "write these entities down and read them back, exactly". One module family with a stable on-disk identity for components and entities, a record store interface that any backend can implement, and a SQL database interface under the first store means that the save format is decided once, tested once, and survives type changes.

Goals: a round trip that is bit-exact for every stored component; versioned (an older stored version loads through registered upgrades); incremental (a save writes what changed); atomic (a crash leaves the old or the new state, never a mix); a database interface with two implementations (SQLite and an in-memory one) that pass one conformance suite; a stored file is untrusted input.
Non-goals: a binary save-game file store, a network store, a cloud store (the interface is shaped for them; none is built); storing ECS resources, spatial sets, render handles or any derived structure; streaming of world cells (Phase 33); the asset format (Phase 22 reuses the registry and the identity rules, not the database); SQL features beyond the portable subset of section 4.
Anti-goals: SQL text built from data; a SQLite type, code or header reaching any file outside `engine/database/sqlite/`; a load that leaves a half-built world; a decoder that trusts a length it read.

## 2. Decisions

| Topic | Decision | Reason |
|---|---|---|
| Modules | `engine/database` (the interface and `engine/database/sqlite/`, the only place that includes `sqlite3.h`) depends on `foundation` only. `engine/storage` (registry, identities, `Store`, `MemoryStore`, `SqlStore`, `WorldStorage`) depends on `foundation`, `ecs`, `jobs` and `database`. `storage`'s public headers name no SQL type; `database` names no ECS type. | `database` is reusable by tools and servers without an ECS; the one-way edge keeps `ecs` unaware of storage. |
| Two interfaces | `storage::Store` is a **record store** (put, get, remove, scan in key order, an all-or-nothing batch, metadata). `storage::WorldStorage` is a non-virtual layer that reads a `World` through the registry, writes records, and loads them back (`Save`, `Load`, `Delete`, `List`, async forms). The phase file's `Store::Save(world, ...)` is realised as `WorldStorage::Save`. | A file store or a network store then implements record I/O only; the ECS-side logic is written once. |
| Registry | A component is registered with a stable `u32` id written in code (checked unique, never the in-memory type id), a unique name, a version, and either a field list by pointer to member or a custom save/load pair; upgrade functions step `v -> v+1`. A `Derived` flag marks components rebuilt on load (`WorldTransform`, `HierarchyDepth`, `WorldBounds`, `SpatialProxy`) and a `Skip` flag marks runtime-only ones. `UnregisteredWithData(world)` lists any component that carries data and is neither registered nor flagged. | Names and ids survive builds; a forgotten new component is found by a check, not by a lost save. |
| Encoding | Canonical little-endian; integers fixed width; `f32` and `f64` as bit patterns (NaN payload, `-0` and denormals survive); `bool` one validated byte; enums as their underlying integer with a declared range; strings as a bounded varint length plus UTF-8 (validated); arrays as a bounded count plus elements; entity references as 16-byte keys; no padding. | Bit-exact round trips and a byte format that does not depend on struct layout. |
| Identities | `EntityKey`: 128 random bits, stored as a component; a `KeyIndex` resource maps key <-> entity; entity ids are never stored. `EntityRef` fields store a key and are resolved in a second pass after every entity of a load exists. A missing target is `MissingReference` (with the first keys) or, by option, a null reference and a count. | Identity survives processes and machines; a reference never dangles silently. |
| What is stored | Components of entities only, selected by "every entity with a key", a key set, or an ECS query plus a filter. ECS resources stored: none. Derived state is rebuilt by running the transform and spatial systems once after a load. | The smallest stable surface; everything derived can change without a format change. |
| Entity order on load | Entities are created in **ascending key order** (the store's scan order), grouped by archetype so chunks fill in order. The same stored state into an empty world gives the same entity order and archetype layout. | Determinism for replays and tests, and a scan the database does cheaply (section 3, spike b). |
| Layouts | **Default: one row per entity** holding a framed container blob (a count, then for each component its id, version, length and bytes) plus a per-entity content hash. **Optional: column-per-field tables** (`c_<name>`) for components flagged `Queryable`, written alongside; the blob stays the source of truth for loading. | Spike (b): the entity blob inserts 3.2x faster and loads 4x faster than a row per component and is half the size; columns cost 2x to insert and 3.5x to load but answer field queries in milliseconds. |
| Insert order | A full save sorts its records by key before inserting. | Spike (b): key-sorted insertion of 1,000,000 entities takes 0.88 s against 5.4 s in random key order. |
| Incremental saves | Inside a process a `SaveSession` remembers the world tick of its last save and visits only chunks whose written-ticks are newer; destroyed entities come from the ECS removal log (a bounded log: on overflow the session falls back to a key-set difference against the store and reports it). Records are rewritten only if their content hash differs. The first save of a process has no remembered tick and diffs by the stored hash. | Saving a large idle world costs a chunk-stamp scan; unchanged entities are never rewritten, also after a restart. |
| Atomicity | A save is one store batch (one database transaction). A load decodes and validates every record into a staging area first and applies it to the world in one step, on the caller's thread: a corrupt, truncated or cancelled load leaves the world unchanged. | All or nothing on both sides. |
| Database interface | `Database::Open` gives a `Connection`; `Prepare(SqlText)`, `Execute(SqlText)`, transactions with a rolling-back guard, savepoints, `Statement` bind by index and name, `Step`, typed columns, `Reset`; `Status` with a portable `ErrorCode` and the backend's message; `Supports(Feature)`. `SqlText` is constructible only from a string literal (`consteval`), so SQL cannot be built from data; the gate `database_sql_check` is the second line. | The rule "no SQL by concatenation" holds by construction. |
| SQLite | Public domain amalgamation, vendored under `third_party/sqlite/`, pinned by version and SHA-256 (Day 251; the version is the newest stable release then, 3.54.1 at the time of writing), compiled with `SQLITE_OMIT_LOAD_EXTENSION`, `SQLITE_OMIT_DEPRECATED`, `SQLITE_OMIT_SHARED_CACHE`, `SQLITE_DQS=0`, `SQLITE_DEFAULT_FOREIGN_KEYS=1`, `SQLITE_THREADSAFE=2` (multi-thread: a connection is used by one thread at a time), `SQLITE_OS_OTHER=1` (the VFS below is the only OS layer), no shell. Only `engine/database/sqlite/` includes the header; `sqlite_boundary_check` enforces it. | A correct crash-safe SQL engine is not engine-differentiating work (`PLAN.md` assumption #25). |
| VFS | **SQLite reaches the disk through a VFS built on `io::File`** (spike a): `xRead`/`xWrite` at offsets, `xTruncate`, `xSync` -> `File::Sync`, `xFileSize`, `SyncDirectory` after creating a journal; **in-process lock tables** with the real shared/reserved/pending/exclusive rules plus a lock file that refuses a second process; **heap-backed WAL index** (the shared-memory methods over process memory). No entry on the `file_access_check` allow list. | The spike's prototype on the same primitives passes 600 injected-failure runs; the rules of `file_access_check` and the platform backends stay intact. |
| Journal mode | Default `WAL` with `synchronous=NORMAL`; `DELETE` and `TRUNCATE` selectable by setting; WAL requires the version-2 VFS with the shared-memory methods (a version-1 VFS silently stays in `DELETE`, spike a). A database file is used by one process. | WAL gives readers that do not block the writer, which the async forms need. |
| Threads | One writer at a time per database (a second writer gets `Busy` after the timeout); readers on worker jobs between batches; a `Connection` and its `Statement`s are used by one thread at a time (a debug assert checks it). | Matches SQLite's model and keeps the contract simple. |
| Untrusted input | Every length is bounded by a setting (`storage.max_record_bytes`, `storage.max_string_bytes`, `storage.max_array_elements`, `storage.max_components_per_entity`); every decode is bounds-checked and returns a `Status`; a corrupt SQLite file is an error value from the open path (`database.integrity_check` runs `quick_check` on open). Fuzzed and sanitizer-tested. | A save game from the internet must not be able to crash or exhaust the process. |
| Settings | `storage.path`, `storage.layout` (`blob`, `columns`), `storage.autosave_seconds`, the four limits above, `database.journal_mode`, `database.synchronous`, `database.busy_timeout_ms`, `database.integrity_check`, `database.statement_cache`. | Everything configurable is a setting (Phase 11). |
| Unknown data on load | Policy in `LoadOptions`: `Fail`, `SkipAndReport`, or `KeepOpaque` (the bytes of an unregistered component survive and are written back on the next save). A record newer than the registered version is `TooNew`. | An older program can open and re-save a newer file without destroying it, if the user chooses. |
| Not in this phase | A binary file store, a network store, a cloud store, resources, world streaming, SQL beyond section 4. | Scope. |

## 3. Spikes

All four are throwaway programs (not committed; they use `std` and the system SQLite because they are not engine code).

### (a) A SQLite VFS over `io::File`'s primitives

`io::File` offers read and write at an offset, `Flush`, `Sync`, `Truncate` and `Size`; it has no file locking and no shared memory. A prototype VFS over `pread`, `pwrite`, `fdatasync`, `ftruncate` and `fstat` with no-op locks and a heap-backed shared-memory region (one connection):

| Check | Result |
|---|---|
| 200,000 inserts (100-byte blobs) in one transaction, `synchronous=FULL` | 54-62 ms in `DELETE` and `TRUNCATE`, 76 ms in `WAL`; reopen: all rows, `integrity_check` ok |
| `journal_mode=WAL` with a version-1 VFS (no shared-memory methods) | **refused silently**: the mode stays `delete`, with or without `locking_mode=exclusive` |
| `journal_mode=WAL` with a version-2 VFS and the four shared-memory methods over heap memory | works: `journal_mode` reads back `wal` |
| Fault injection: the Nth `write` fails, N from 0 to the end of a 200-row transaction | `DELETE`: 202 of 202 runs reopen to the old 100 rows or the new 300 (never a mix) with `integrity_check` ok; `WAL` (exclusive and not): 208 of 208 and 208 of 208 |

Conclusion: the VFS route works on exactly `io::File`'s surface. Day 254 adds what the prototype omitted: real in-process shared/reserved/pending/exclusive lock rules (several connections in one process), a lock file that refuses a second process, injected failures of `Sync` and `Truncate` as well as `Write`, and `xFullPathname` through `io::Path`. The fallback (an allow-list entry for the file layer) is not needed.

### (b) Storage layout

100,000 and 1,000,000 entities with six components each (transform 40 B, velocity 12 B, health 8 B, a name, a parent key 16 B, a tag 8 B), 16-byte random keys, WAL, one transaction:

| Layout | N | Insert, random key order | Insert, key-sorted | Full load | File size | Field query `health < 10` |
|---|---|---|---|---|---|---|
| 1. one blob row per component | 100,000 | 925 ms | | 41 ms | 40.1 MB | needs a decode of every row |
| | 1,000,000 | 13,815 ms | 3,533 ms | 414 ms (392 sorted) | 401 MB | |
| 2. one blob row per entity | 100,000 | **293 ms** | | **9 ms** | **18.0 MB** | needs a decode of every row |
| | 1,000,000 | 5,444 ms | **881 ms** | **96 ms** (93 sorted) | **179 MB** | |
| 3. column per field (6 tables + `entity`) | 100,000 | 1,902 ms | | 139 ms (7-way join) | 39.6 MB | **0.35 ms** (indexed, 10,076 hits; index build 21 ms) |
| | 1,000,000 | 31,035 ms | 4,526 ms | 1,569 ms (1,508 sorted) | 397-402 MB | 3.8 ms (100,148 hits; index build 251 ms) |

Conclusions: (1) the entity blob is the default: 3.2x faster to insert and 4x faster to load than a row per component, half the size, and the incremental unit (an entity whose chunk changed) is the same granularity as the ECS change stamps; (2) inserting in key order is worth 3.9x to 6.2x, so a full save sorts; (3) the column layout is only for components that tools and servers query, written alongside the blob, because it costs about 2x on insert and gives a field query in milliseconds at a million entities; (4) "which entities have component X" is not answerable from the entity blob in SQL, which is acceptable because the ECS answers it and a `Queryable` component has its own table. Amendments: Day 262's schema is `entity(key, seq, hash, data)` (no `component` table), Day 263's columns are the optional layout, Day 260's save sorts by key.

### (c) The portable SQL subset (what `SqlStore` may use)

`SqlStore` and the tests use exactly these statements. **Amended 2026-11-11 (the user's decision): there is no in-memory database; it would need its own SQL parser.** SQLite runs them (in memory through `:memory:`), and the list stays the contract so that another backend remains possible; the in-memory *store* is `storage::MemoryStore`, which uses no SQL:

- `CREATE TABLE [IF NOT EXISTS] t (col TYPE [NOT NULL] [PRIMARY KEY] [UNIQUE] [REFERENCES t(col) [ON DELETE CASCADE]], ..., [PRIMARY KEY (c1, c2)])` with `TYPE` in `INTEGER`, `REAL`, `TEXT`, `BLOB`; `CREATE [UNIQUE] INDEX [IF NOT EXISTS] i ON t(c1 [, c2])`.
- `INSERT INTO t (cols) VALUES (...) [, (...)...]` and `INSERT OR REPLACE INTO ...`; values are `?`, `:name` or an integer, real, text or `NULL` literal.
- `SELECT cols | * | count(*) FROM t [WHERE cond [AND cond ...]] [ORDER BY col [ASC|DESC]] [LIMIT n [OFFSET m]]` with `cond` of the form `col op value` (`=`, `<>`, `<`, `<=`, `>`, `>=`, `IS NULL`, `IS NOT NULL`); no joins, no subqueries, no expressions.
- `UPDATE t SET col = value [, ...] [WHERE ...]`, `DELETE FROM t [WHERE ...]`, `DROP TABLE t` (added Day 262 for the schema migration that rebuilds a table; there is no rename).
- `BEGIN`, `COMMIT`, `ROLLBACK`, `SAVEPOINT s`, `RELEASE s`, `ROLLBACK TO s`.
- A schema version through `Connection` (`SchemaVersion()` / `WriteSchemaVersion()`, backed by `PRAGMA user_version` on SQLite and the backend's own field): no `PRAGMA` text in `SqlStore`.
- `ALTER TABLE t ADD COLUMN ...` is in the subset **only behind `Connection::Supports(Feature::AddColumn)`** (Day 262 amendment: `SqlStore` migrations use it when the backend has it, in place); without it a column layout change rebuilds the table through a scratch table (`DROP TABLE` is in the subset, a rename is not).

Joins are not needed: the entity blob layout loads with one ordered scan, and the optional column tables are read by key range, one table at a time.

### (d) SQLite itself

The system library on this machine is 3.54.1 (2026-10-16); the spikes used it. The vendored amalgamation is chosen and hashed on Day 251 (downloading it needs the user's yes, stated with the file name, source and size); the options are listed in section 2. License: public domain (the blessing text ships in `third_party/sqlite/`).

## 4. Benchmark definition and pass condition

Release build, Ryzen 9 7950X3D, NVMe, `database.synchronous=NORMAL`, WAL. Workload: N entities, six components (the spike's set), a hierarchy of depth 5 through `Parent`, random 128-bit keys. Measured by `tests/storage/storage_bench.cpp` (hidden `[.][storage-bench]`) on Day 265.

| Metric | Pass condition | Basis |
|---|---|---|
| Round trip | bit-exact for every component; the same entity order and archetype layout on two loads | correctness, no tolerance |
| Full save, 1,000,000 entities, blob layout, SQLite | at most 5 s (spike: 0.9 s of raw inserts) | encoding and sorting may cost four times the raw insert |
| Full load, 1,000,000 entities | at most 4 s (spike: 0.1 s of raw reads) | entity creation dominates |
| Full save and load, 100,000 entities | at most 10 percent of the 1,000,000 figures plus 20 ms | linear scaling |
| Incremental save, 1 percent of 1,000,000 changed | at most 0.25 s | 10,000 updated records plus the chunk scan |
| Idle incremental save | at most 20 ms at 1,000,000 entities (no record touched) | a chunk-stamp scan |
| Column layout save | at most 2.5x the blob layout | spike: 1.3x-5x depending on key order |
| `FindKeys` on an indexed queryable field, 1,000,000 entities | at most 10 ms | spike: 3.8 ms |
| Asynchronous save, 8 workers against 1 | encoding at least 2x faster | the chunk ranges are independent |
| Crash safety | an injected failure at every write, sync and truncate of a save leaves the old or the new state | spike (a) |
| Hostile files | the corruption fuzz never crashes, hangs or allocates above the limits | untrusted input |

A miss is listed in `KNOWN-ISSUES.md` with the options, as on Phase 18 Day 248.

### Measured (Day 265, 2026-11-11, Linux machine, Release, clang, `storage_tests "[storage-bench]"`)

Six components (key, transform, bounds, membership, an actor with four fields, a flag), a hierarchy of depth up to 5, SQLite file in WAL mode, `synchronous` default.

| Metric | 100,000 | 1,000,000 | Pass condition | |
|---|---|---|---|---|
| Full save, SQLite blob | 245 ms | 2,927 ms | 5 s; 100k at most 312 ms | met |
| Full load, SQLite blob | 274 ms | 3,417 ms | 4 s; 100k at most 361 ms | met |
| Full save / load, memory store | 210 / 135 ms | 2,284 / 1,529 ms | reference | |
| Full save, columns and an index | 446 ms | 6,194 ms | at most 2.5x blob | met (1.8x, 2.1x) |
| Idle incremental save | 0.08 ms | 0.4 ms (1.1 ms with columns) | 20 ms | met |
| Incremental, 1 percent in neighbouring entities | 13 ms | 128 ms (435 ms with columns) | 0.25 s | met (blob) |
| Incremental, 1 percent spread at random | 98 ms | 1,230 ms | 0.25 s | **missed** (93 percent of the entities share a chunk with a changed one) |
| `FindKeys` on an indexed field | 0.09 ms | 1.3 ms | 10 ms | met |
| Async save, snapshot and encoding, 1 vs 8 participants | 110 vs 24 ms | 1,681 vs 364 ms | at least 2x | met (4.6x) |
| Async save, everything, 1 vs 8 participants | 186 vs 107 ms | 2,585 vs 1,317 ms | | |
| Hostile files | 263 corrupted files, other schemas, noise, an oversized record: every case a `Status`, plain and asan | | never crashes or allocates above the limits | met |
| Crash safety | a failure at every write and sync of a batch leaves the old or new state (Day 254, Day 262) | | | met |

### Deviations from Day 250

- `Store::Save(world, ...)` is `WorldStorage::Save`; the incremental form uses a `SaveSession`; `PutMany` is an addition (Day 263).
- The settings opener is `LocalStorage::OpenFromSettings`, not a `WorldStorage` function (a `WorldStorage` borrows its store).
- `SaveAsync` reads the world inside the call (encoding on the workers) and only the store writes are in the background; this is what makes "what is stored is what the frame left" true by construction. The autosaver is a class called once a frame, not an ECS system.
- `LoadAsync` reads the records on a job; decoding and entity creation stay on the caller's thread.
- A `Full` save holds the encoded records of the selection in memory until the write (they are freed as written).
- Not done: the render-scene draw-list comparison in the demo (needs Phase 22 assets); exclusion of a second process on one file (the file layer has no file locks).

## 5. Contracts the other phases rely on

- `ByteWriter` and `ByteReader` (the bounds-checked canonical encoding) live in `foundation/bytes/`, below both `storage` and Phase 20's `net`, so the two share one reader; this amends the module row of section 2 (`storage` uses it, does not own it).
- Phase 22 (assets): `ComponentRegistry`, `ByteWriter`/`ByteReader`, the version and upgrade rules and `EntityKey` are the level and prefab format's building blocks; the asset format is minted, immutable and deterministic, not stored in a database.
- Phase 20 (networking): a network store implements `storage::Store`'s record operations; nothing in `WorldStorage` changes.
- Phase 30 (editor): scenes are saved through `WorldStorage`; `FindKeys` serves the editor's queries.
- Phase 33 (world streaming): cells are selections by key set; `Merge` loads them into a running world.
- Phase 18 (spatial): spatial membership and bounds components are stored; proxies and world bounds are derived and rebuilt on load.
