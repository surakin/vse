# Phase 19 / Day 255 — SQLite Backend III: Settings, Statement Cache, Concurrency & Benchmark

**Phase:** [Phase 19 — Storage](../phase-19-storage.md)
**Depends on:** Days 253-254 (statements, transactions and error mapping are done on Day 253).

Finish the SQLite implementation: settings, the statement cache, concurrency, and the whole conformance suite on files and in memory.

## Tasks

- [x] **Statement cache:** a per-connection prepared-statement cache (size from a setting), keyed by the statement text SQLite keeps (`sqlite3_sql`; a literal's address is not stable across `SqlText::Unchecked`); a cached statement is reset on reuse; the statement object keeps its connection alive or fails safely after the connection closed (Day 253 tests the failure; this day the cache).
- [x] **Write transactions begin `IMMEDIATE`** in the `Transaction` guard's default; the `Savepoint` family under concurrency.
- [x] **Settings** `database.journal_mode`, `database.synchronous`, `database.busy_timeout_ms`, `database.integrity_check`, `database.statement_cache` registered through `settings::Registry` (`RegisterDatabaseSettings`), read when a connection opens; defaults from the Day 250 spec; `config_access_check` needs no entry.
- [x] **Concurrency:** one writer and many readers across connections (WAL, or the Day 250 fallback), a second writer gets `Busy` after the timeout and never corrupts; a connection is used by one thread at a time (documented, the debug build asserts it).
- [x] **Conformance:** the Day 252 suite passes on SQLite in plain, asan and tsan, on both fixtures (`:memory:` and a file); no case skips except those a fixture's traits exclude.
- [x] **Micro-benchmark** (`[database-bench]`, hidden): inserts per second in one transaction (prepared, 100,000 rows), point selects per second, scan rate, on a file and on `:memory:`; recorded in the day file.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (most days of this phase do); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [x] **Two machines (CLAUDE.md "Windows is a supported platform"):** the code is portable; it is built and tested on the Windows PC no later than Day 265, and until then the day is listed as "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page named in the day is updated or created and its tagged samples compile and run.
- [x] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 19).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the SQLite backend is complete (settings, cache, concurrency), passes the conformance suite in memory and on files in every flavor, and is used only through the interface.

## Notes

- Settings: `engine/database/database_settings.{h,cpp}` (`RegisterDatabaseSettings`, `OptionsFromSettings`); `OpenOptions` gained `integrity_check` and `statement_cache`. The `Transaction` guard already defaulted to `Immediate`; a savepoint test under a second connection was added.
- Statement cache: finished statements are reset, unbound and parked in the connection's handle (oldest evicted at capacity), found again by comparing the text `sqlite3_sql` reports. Text with a trailing comment after the `;` never hits (harmless).
- Thread check: `UseGuard` in `Prepare`/`Step`, active only with `ENGINE_SLOW_CHECKS`.
- tsan: SQLite's lock-free WAL index header read is reported; `tests/database/tsan.supp` suppresses `walIndexTryHdr` and `walIndexWriteHdr` only (the user may want to review this choice).
- Benchmark (Linux, Clang, plain, 20,000 rows, file, write-ahead log): inserts 570,689/s (one transaction), cached point reads 500,598/s, uncached 239,517/s. The plan's 100,000-row / scan / `:memory:` variants were not taken.
- The suite on both fixtures passes in plain, asan and tsan (memory fixture skips its 2 shared-location cases by trait).
- Runs (Linux, Clang): plain `ran 2340 of 6131 tests (phases: all; flavors: plain; display: included; crash tests: excluded)` (known failures: two X11Window tests); asan `ran 1807 of 6134 tests`; tsan `ran 1489 of 6134 tests` (known failure: the Day 235 scene test). Clang non-unity and GCC: `database_tests` 128 of 128 run, 94 passed, 34 skipped (stub fixture). Gates and `ctest -L docs` pass; database documented at the baseline.

## Post-implementation code review

Inline review of the day's diff (not the `/code-review` skill): no correctness defects found. Observations: a cached statement is not found when the text has a comment after its first statement (accepted, noted above); the benchmark does not cover scans or `:memory:` (accepted). Nothing needed fixing.
