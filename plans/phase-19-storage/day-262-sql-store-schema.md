# Phase 19 / Day 262 — SqlStore I: Schema, Blob Layout & Migrations

**Phase:** [Phase 19 — Storage](../phase-19-storage.md)
**Depends on:** Days 252-255, 259-261.

Put the record store on a SQL database, through the interface only.

## Tasks

- [x] **`storage::SqlStore`** implementing `storage::Store` on a `database::Connection` (it owns or borrows one; the choice is documented); only the portable statement subset of Day 250 spike (c), so it runs on `MemoryDatabase` and `SqliteDatabase` alike; every statement is prepared once and bound, none is built from data.
- [x] **Schema (entity-blob layout, Day 250 spike (b)):** `meta(key TEXT PRIMARY KEY, value BLOB)` and `entity(key BLOB PRIMARY KEY, seq INTEGER NOT NULL, hash BLOB NOT NULL, data BLOB NOT NULL)`, one row per entity; `data` is the framed container (a component count, then per component its id, version, length and bytes) built by the codec; no `component` table; the schema version is `Connection::SchemaVersion()`.
- [x] **Schema migrations:** an ordered list of steps, the version recorded in `meta`, applied in one transaction at open; a database with a newer schema than the program knows is refused (`TooNew`) and left untouched; the previous schema's creation code is kept in `tests/storage/old_schema.cpp` so the upgrade test builds a real old database.
- [x] **Batches:** `Begin`/`Commit`/`Rollback` map to database transactions; a `Put` batch of N records is one transaction; a failure rolls everything back.
- [x] **Tests** (`tests/storage/sql_store_test.cpp`): the store conformance suite passes on `SqlStore` over SQLite in memory and on a file; migration from the old schema keeps every record; opening a newer schema is refused; a crash injected at every write of a save (Day 254's wrapper) leaves the old or the new state; `PRAGMA`-free: no SQLite-only statement is used (the portable subset of the spec, section 3 (c), so a later backend can implement it).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (most days of this phase do); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** the code is portable; it is built and tested on the Windows PC no later than Day 265, and until then the day is listed as "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page named in the day is updated or created and its tagged samples compile and run.
- [x] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 19).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** worlds are saved to and loaded from a SQL database through the database interface, atomically, with schema versions.

## Notes (2026-11-11, Linux machine)

- `storage::SqlStore` (`sql_store.h/.cpp`): borrows a `database::Connection`, `SqlStore::Open` migrates and prepares eight statements once (put, get, remove, two scans, count, two for metadata). Layout `entity(key BLOB PK, seq INTEGER, hash BLOB, data BLOB)` with the key as 16 and the hash as 8 big-endian bytes (the database's order is key order), `meta(key TEXT PK, value BLOB)`. Batches are `Immediate` transactions. Only the portable subset is used; `DROP TABLE` was added to the subset in the design for the migration (there is no rename in it).
- Migration (amended after the first commit): the 1 to 2 step uses `ALTER TABLE entity ADD COLUMN seq` plus `UPDATE ... SET seq = 0` when `Supports(AddColumn)`, else the scratch-table rebuild; a test runs both paths (a connection decorator hides the feature). Schema version 1 (no `seq`) to 2 goes through a scratch table, in one transaction at open; the version is `Connection::SchemaVersion()` and also the metadata `schema_version`. A newer schema is `TooNew` and untouched. `tests/storage/old_schema.{h,cpp}` builds a real version-1 database independently of the store.
- Decision: one connection serves the store, so while a batch is open other threads get `Busy` for reads too (documented; the conformance case "readers see the old state" is switched by `Fixture::kReadersSeeCommitted`, true for MemoryStore). A second read connection would need a file and was not worth the complexity.
- Tests (`sql_store_test.cpp`): the conformance suite on SQLite in memory (with a corrupted-record case) and on a file; migration of 50 records, repeated open; newer schema refused; reopen of a file and key order across the high half; Busy from another thread; a failure at every write and sync of a batch (crash and plain failure, with the file layer's fault plan) leaves the old or the new state, metadata with it; a world saved incrementally to a SQL file and loaded back equal.
- Docs: guide section "The SQL store" with a compiled sample (docgen links `database_sqlite`, `database`, `sqlite3`); `ARCHITECTURE.md`/`modules.txt` say the database edge is in force.
- Runs: plain `ran 2472 of 6485 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (two known X11 failures); asan `ran 1917 of 6485`; tsan `ran 1599 of 6485` (only the known Day 235 failure). GCC and Clang non-unity builds clean; `docs` target and `ctest -L docs` pass.

## Post-implementation code review

`/code-review high`: 2 findings. (1) `SqlStore::Open` made the store with plain `new` while `UniquePtr` frees with the heap allocator: fixed (public constructor, `MakeUnique`). (2) Reads from other threads are `Busy` during a batch, unlike MemoryStore: declined, it is the single-connection design and is documented and tested.
