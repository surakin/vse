# Phase 19 / Day 253 — The In-Memory Database

**Phase:** [Phase 19 — Storage](../phase-19-storage.md)
**Depends on:** Day 252; the statement list of Day 250 spike (c).

A small database that implements the interface in memory, as the test double that proves the interface does not leak SQLite, and as the fast backend for the storage tests.

## Tasks

- [ ] **`database::MemoryDatabase`:** tables with typed columns (`INTEGER`, `REAL`, `TEXT`, `BLOB`), `PRIMARY KEY` (single and composite), `UNIQUE`, `NOT NULL`, `FOREIGN KEY ... ON DELETE CASCADE`; an ordered index per primary key and per `CREATE INDEX`; rows pulled one at a time through `Statement::Step`.
- [ ] **The restricted statement set** exactly as listed in section 3 (c) of `docs/superpowers/specs/2026-11-11-storage-design.md`: `CREATE TABLE [IF NOT EXISTS]` (with `PRIMARY KEY`, composite keys, `UNIQUE`, `NOT NULL`, `REFERENCES ... ON DELETE CASCADE`), `CREATE [UNIQUE] INDEX`, `INSERT` and `INSERT OR REPLACE` (also several rows per statement), `SELECT` of columns, `*` or `count(*)` with `WHERE` of `col op value` conditions joined by `AND` (including `IS [NOT] NULL`), `ORDER BY`, `LIMIT/OFFSET`, `UPDATE ... WHERE`, `DELETE ... WHERE`, `BEGIN`/`COMMIT`/`ROLLBACK`, the `SAVEPOINT` family; no joins, no subqueries, no expressions, no `ALTER TABLE`; parameters by index (`?`) and by name (`:name`); the schema version through `Connection::SchemaVersion()`.
- [ ] **Anything else is `Unsupported`, never undefined:** a statement outside the set returns a `Status` with `Unsupported` and a message naming the first unrecognised token; `Supports` reports what the memory database lacks (`WriteAheadLog`, `ConcurrentReaders` as snapshot reads only).
- [ ] **Transactions** through an undo log (rollback restores every table exactly); savepoints nest; a constraint error inside a statement leaves the table as it was before that statement.
- [ ] **Conformance:** the Day 252 suite passes on `MemoryDatabase` (cases for absent features skip with the reason shown).
- [ ] **Tests** (`tests/database/memory_database_test.cpp`): a randomized comparison of table contents with a brute-force model after 5,000 random statements (insert, replace, update, delete, rollback to savepoints); a statement fuzz (random token strings, 100,000 cases) never crashes and always returns a `Status`; the same statement text run twice gives the same row order.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (most days of this phase do); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the code is portable; it is built and tested on the Windows PC no later than Day 265, and until then the day is listed as "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page named in the day is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 19).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the interface has a second implementation that passes the conformance suite, and a statement it does not know is an error value.
