# Phase 19 / Day 265 — Benchmarks, Hostile Files, Docs, Windows PC, Closing Full Run & Phase Review

**Phase:** [Phase 19 — Storage](../phase-19-storage.md)
**Depends on:** Days 250-264.

Measure against the Day 250 pass condition, attack the load path with hostile files, finish the documentation, run the Windows PC and close the phase.

## Tasks

- [x] **Benchmarks, against the pass condition in section 4 of the spec** (`tests/storage/storage_bench.cpp`, hidden `[.][storage-bench]`, Release): save and load of 100,000 and 1,000,000 entities (six components, depth-5 hierarchy), full and incremental (1 percent changed, and idle), blob and column layouts, SQLite and the memory store, async with 1 and 8 workers; results table in the spec compared with the pass condition; misses are listed in `KNOWN-ISSUES.md` with the options, not silently accepted.
- [x] **Hostile files:** a deterministic corruption fuzz over a saved SQLite file (byte flips, truncation, page swaps, a valid database of another schema, a file that is not a database, an oversized record) through the open and load path, plain and asan: every case is a `Status` (the `database.integrity_check` setting is exercised), no crash, no allocation above the limits; a save game from outside is documented as untrusted in the guide.
- [x] **Guides** `docs/guide/storage.md` and `docs/guide/database.md` finished (concepts, the registry rules for adding a component, versioning and upgrades, identities, incremental saves, layouts, settings, threading, error table, the untrusted-input rules, how a new store is added); the tutorial run; `docgen lint` at zero; `cmake --build build --target docs` output committed.
- [x] **Spec** updated to what was built, with the measured tables and every deviation from Day 250.
- [x] **Windows PC (done 2026-11-11):** build and test with `windows-msvc` (plain and asan) and `windows-clang-cl`, both unity modes; the VFS on the Windows file backend; `tests/database`, `tests/storage`, `storage_demo_runs`; fix what breaks or list it in `KNOWN-ISSUES.md`.
- [x] **Closing run (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --all --flavor all` on Linux (and the Windows PC's flavors), plus `ctest -L docs` and the demos the phase ships; quote the final line (`ran X of Y tests (phases ...)`) in this file; `CURRENT_PHASE` stays 19 until Phase 20's first day.
- [x] **Uncapped end-of-phase review:** `/code-review high --max-findings all` over the whole phase's diff (from the commit before Day 250 to the working tree); verify, fix or decline each finding with a reason; record the count and outcome under "Post-implementation code review".
- [x] **`PLAN.md`, `STATUS.md`, the phase file and `KNOWN-ISSUES.md`** updated: phase status, progress log, the open items (anything missed or waiting for the other machine).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (most days of this phase do); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page named in the day is updated or created and its tagged samples compile and run.
- [x] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 19).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** storage and the database interface are measured, documented, hardened against hostile input, run on both platforms, and reviewed; Phase 19 is complete.

## Notes

- Benchmarks (Release) and the one missed pass condition (random 1% incremental, 1.23 s against 0.25 s) are in the spec's "Measured" table and `KNOWN-ISSUES.md`; no decision taken.
- Hostile files: `tests/storage/hostile_file_test.cpp` (corruption fuzz, oversized record, structural component id), plain and asan.
- Windows PC task **not done** on the Linux machine; done on the Windows PC the same day (next item).
- **Windows PC (2026-11-11):** after the pull, the four Windows trees (`build-msvc` plain and asan, `build-clang-cl`, `build-windows-msvc-nounity`,
  `build-windows-clang-cl-nounity`) built clean with nothing to fix (Days 251-263 had been caught up earlier that day, three fixes, Day 263 file).
  Closing run: `ran 4487 of 4487 tests (phases: all; flavors: all (plain and asan, no tsan on Windows); display: included; crash tests: included) in 330 s`. Two failures, both docs: the tutorial
  `docs/tutorials/saving-a-world.md` ignored the `[[nodiscard]]` results of `Autosaver::Update` and `Flush` (MSVC C4834; the sample now checks them),
  and `docs/reference/jobs.md` and `symbols.txt` were out of date after the Mac's Day 305 (written as Day 289 before the renumbering) comments (regenerated); `ctest -L docs` then 7 of 7.
  `storage_demo_runs` and `storage_demo_columns_runs` pass inside the run, as do the hostile-file and async tests in both flavors. Benchmark,
  Release (`build-msvc-release`, MSVC 14.51, Ryzen AI 7 350, NVMe; `storage_tests "[storage-bench]"`, two runs, every check passed): 1,000,000 entities: blob save 3,840-3,857 ms and load 3,231-3,244 ms (pass: 5 s and 4 s, met), memory store 2,295-2,306 / 1,818-1,819 ms, columns and an index 7,219-7,277 ms (1.9x blob, met), idle incremental 1.8 ms (6.2 ms with columns; met), 1 percent in neighbouring entities 381-392 ms (1,075-1,077 ms with columns; **missed**, 0.25 s), 1 percent at random 1,490-1,498 ms (missed, as on Linux), `FindKeys` 1.4-1.9 ms, async snapshot and encoding 1,684-1,702 ms with 1 participant against 379-380 ms with 8 (4.5x, met), everything 2,281-2,311 against 980-986 ms. 100,000 entities: blob save 351 ms in the second run (625 ms in the first, the first save of that size) and load 300-312 ms, memory store 199-200 / 159-160 ms, columns 624-636 ms, idle incremental 3.7-3.8 ms, neighbouring 49 ms, random 118-120 ms, `FindKeys` 0.18 ms, async 138-140 against 30 ms.
  Review of the tutorial fix (`/code-review high`): no findings.
- Closing run on Linux: `ran 6556 of 6556 tests (phases: all; flavors: all; display: included; crash tests: included) in 263 s`. Failures: three known X11 window tests (locked session) and the known tsan scene segfault (Day 235); nothing new. `ctest -L docs`: 7 of 7; docs 8273/8273; demos `storage_demo_runs` and `storage_demo_columns_runs` pass inside the run. GCC and Clang non-unity trees build clean.

## Post-implementation code review

Uncapped review in three parts (storage core, SQL store and tests, database module), about 60 findings in total.
- **Fixed:** a stored record naming a structural component id loaded (now `Corrupt`; regression test, confirmed failing without the fix); a dropped `AsyncSave` handle left the session stale (now adopts; regression test, confirmed failing without the fix); `SqlStore` Put/PutMany/Remove not atomic outside a batch in column layout; `Rollback` cleared its flag before the database let go; full-save removal of a vanished record failed with `NotFound`; component names with control characters broke the manifest; `storage_demo` ctests lacked the profile environment. The last five have no dedicated test.
- **Filed, not fixed:** the remainder, each with its reason, in `KNOWN-ISSUES.md` (Day 265 entries) - mostly design limits (second-process WAL exclusion, read-only VFS fallback, crash model fidelity) and edge cases that need a decision from the user.
