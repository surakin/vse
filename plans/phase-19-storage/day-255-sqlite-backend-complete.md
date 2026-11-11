# Phase 19 / Day 255 — SQLite Backend II: Statements, Transactions, Errors & Concurrency

**Phase:** [Phase 19 — Storage](../phase-19-storage.md)
**Depends on:** Day 254.

Finish the SQLite implementation of the interface and run the whole conformance suite on it.

## Tasks

- [ ] **Statements:** `Prepare` over `SqlText`, binding every `Value` kind (text and blobs bound by copy unless the caller's `ValueView` is documented to outlive the step), by index and by name, typed column getters with a type check, `Reset`/`ClearBindings`, a per-connection prepared-statement cache (size from a setting); the statement object keeps its connection alive or fails safely after the connection closed (documented, tested).
- [ ] **Transactions and savepoints:** write transactions begin `IMMEDIATE`; the guard rolls back on destruction; `Savepoint` family mapped; `LastInsertRowId` and `ChangedRows`.
- [ ] **Error mapping:** a table from SQLite primary and extended result codes to `ErrorCode` (constraint kinds, `BUSY`, `LOCKED`, `CORRUPT`, `NOTADB`, `FULL`, `IOERR` family, `READONLY`, `TOOBIG`, `MISUSE`) with the backend message kept; no SQLite code or `sqlite3_*` type reaches a public header.
- [ ] **Settings** `database.journal_mode`, `database.synchronous`, `database.busy_timeout_ms`, `database.integrity_check`, `database.statement_cache` registered through `settings::Registry` (`RegisterDatabaseSettings`), read when a connection opens; defaults from the Day 250 spec; `config_access_check` needs no entry.
- [ ] **Concurrency:** one writer and many readers across connections (WAL, or the Day 250 fallback), a second writer gets `Busy` after the timeout and never corrupts; a connection is used by one thread at a time (documented, the debug build asserts it).
- [ ] **Conformance:** the Day 252 suite passes on SQLite in plain, asan and tsan; the same suite file runs both backends (`MemoryDatabase` and `SqliteDatabase`) in one test binary.
- [ ] **Micro-benchmark** (`[database-bench]`, hidden): inserts per second in one transaction (prepared, 100,000 rows), point selects per second, scan rate, for both backends; recorded in the day file.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (most days of this phase do); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the code is portable; it is built and tested on the Windows PC no later than Day 265, and until then the day is listed as "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page named in the day is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 19).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the database interface has two implementations that pass the same conformance suite, and SQLite is used only through the interface.
