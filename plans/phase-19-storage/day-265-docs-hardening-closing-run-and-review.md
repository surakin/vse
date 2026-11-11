# Phase 19 / Day 265 — Benchmarks, Hostile Files, Docs, Windows PC, Closing Full Run & Phase Review

**Phase:** [Phase 19 — Storage](../phase-19-storage.md)
**Depends on:** Days 250-264.

Measure against the Day 250 pass condition, attack the load path with hostile files, finish the documentation, run the Windows PC and close the phase.

## Tasks

- [ ] **Benchmarks** (`tests/storage/storage_bench.cpp`, hidden `[.][storage-bench]`, Release): save and load of 100,000 and 1,000,000 entities (six components, depth-5 hierarchy), full and incremental (1 percent changed, and idle), blob and column layouts, SQLite and the memory store, async with 1 and 8 workers; results table in the spec compared with the pass condition; misses are listed in `KNOWN-ISSUES.md` with the options, not silently accepted.
- [ ] **Hostile files:** a deterministic corruption fuzz over a saved SQLite file (byte flips, truncation, page swaps, a valid database of another schema, a file that is not a database, an oversized record) through the open and load path, plain and asan: every case is a `Status` (the `database.integrity_check` setting is exercised), no crash, no allocation above the limits; a save game from outside is documented as untrusted in the guide.
- [ ] **Guides** `docs/guide/storage.md` and `docs/guide/database.md` finished (concepts, the registry rules for adding a component, versioning and upgrades, identities, incremental saves, layouts, settings, threading, error table, the untrusted-input rules, how a new store is added); the tutorial run; `docgen lint` at zero; `cmake --build build --target docs` output committed.
- [ ] **Spec** updated to what was built, with the measured tables and every deviation from Day 250.
- [ ] **Windows PC:** build and test with `windows-msvc` (plain and asan) and `windows-clang-cl`, both unity modes; the VFS on the Windows file backend; `tests/database`, `tests/storage`, `storage_demo_runs`; fix what breaks or list it in `KNOWN-ISSUES.md`.
- [ ] **Closing run (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --all --flavor all` on Linux (and the Windows PC's flavors), plus `ctest -L docs` and the demos the phase ships; quote the final line (`ran X of Y tests (phases ...)`) in this file; `CURRENT_PHASE` stays 19 until Phase 20's first day.
- [ ] **Uncapped end-of-phase review:** `/code-review high --max-findings all` over the whole phase's diff (from the commit before Day 250 to the working tree); verify, fix or decline each finding with a reason; record the count and outcome under "Post-implementation code review".
- [ ] **`PLAN.md`, `STATUS.md`, the phase file and `KNOWN-ISSUES.md`** updated: phase status, progress log, the open items (anything missed or waiting for the other machine).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (most days of this phase do); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page named in the day is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 19).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** storage and the database interface are measured, documented, hardened against hostile input, run on both platforms, and reviewed; Phase 19 is complete.
