# Phase 21 / Day 294 — Text, UI, ECS and Spatial on macOS (the CPU Modules)

**Phase:** [Phase 21 — macOS Port + Metal Backend](../phase-21-macos-metal.md)
**Depends on:** Day 293.

The modules above the foundation that have no platform code: `engine/text` (with vendored FreeType), `engine/ui` (CPU side, with the header-only
`platform_input`), `engine/ecs`, `engine/spatial`, and any module the main line has added by today at the same layer (check `ARCHITECTURE.md` when the
day starts). They should build unchanged; today proves it and fixes what Apple clang and libc++ find.

## Tasks

- [x] **Inventory at the start:** list the modules on `master` at layers 10-11 that need no window or GPU (`ARCHITECTURE.md`), and their test
      directories; any module added since the plan was written is included here and recorded.
      **Amended (2026-11-12, the user's decision):** the main line added `engine/database` (with vendored SQLite, Phase 19), `engine/storage`
      (Phase 19) and `engine/net` (Phase 20, still in progress). `database` and `storage` join today: they are window-free and their only file
      access is the file layer. `net` has platform code (Linux: `net/posix/` plus `net/linux/`, the `epoll` poller), so its macOS backend is a day of
      its own, [Day 310](day-310-macos-networking.md), after Phase 20 closes on the main line; the later days were renumbered (the demos day is now
      311, the closing day 312). Until then the Mac builds only `net_access_check`, and `KNOWN-ISSUES.md` says so.
- [x] **FreeType** (`third_party/freetype/`) builds on macOS with our CMake (its own flags kept; no `-framework` dependency); the vendored build options
      unchanged; `text/font_freetype.cpp` compiles with Apple clang.
- [x] **Configure and build** `engine/text`, `engine/ui`, `engine/ecs`, `engine/spatial`, `engine/database` (and SQLite) and `engine/storage` and
      their tests on macOS (`tests/text`, `tests/ui`, `tests/ecs`, `tests/spatial`, `tests/database`, `tests/storage`), the CPU-only parts; tests that need a window or a device stay off until Days 295-308 and are listed.
      (Off until Day 295: `tests/ui`'s `ui_window_test`, `clipboard_test`, `viewport_test` and `ui_demo_app_test` with the demo sources it
      compiles, which link `ui_window` and the window layer. `engine/platform` gives only its header-only `platform_input` on macOS until then.
      Everything built at the first attempt, FreeType and SQLite included, with no warning.)
- [x] **Suites:** all pass in plain, asan and tsan; the ECS parallel scheduler and the spatial sync under TSan are the concurrency checks of the day.
      Record any result that differs from Linux (floating-point in geometry or layout tests: the same inputs must give the same outputs; a difference is a
      bug or an unstated tolerance, not a macOS exception).
      (All six suites pass in the three flavors with no sanitizer report and no result that differs from Linux. The skips are the same as on
      Linux: the ECS ratio guard in a Debug build, the database conformance suite's stub backend, and the allocation-counting cases under a
      sanitizer, which owns `operator new`. `tests/database` now gives TSan its SQLite suppressions on macOS too.)
- [x] **Text output determinism:** the glyph atlas and layout tests compare against the same expected values as Linux (FreeType is vendored, so the bytes
      must match); a mismatch is investigated (compiler `-ffp-contract`, FMA on this CPU) and fixed in flags or code.
      (`Rasterized Hello matches the committed goldens` compares the atlas byte for byte with the PGM files committed from Linux: it passes in
      the three flavors, so nothing needed investigating.)
- [x] **Benchmarks** that are CPU-only (`ecs_bench`, the spatial benchmarks): run once and recorded beside the other machines' numbers in their guides, no
      targets.
      (Release tree `build-macos-release`; one run each, the load average at 6 and Spotlight indexing the new tree, so read the ratios more than
      the times. `ecs_bench`: iteration at 100k 0.85 ms against the baseline's 16.9 ms (19.8x), at 1M 9.1 ms against 338 ms (37x); parallel
      efficiency 96.9 percent at 2 workers, 82.4 at 4, 61.3 at 8 (8 threads on 4 cores); churn 4.7 ms against 7.1 (1.5x); sparse 518x; changed
      clustered 37.5x, random 10.8x; hierarchy 6.4 ms against 5.7 (0.89x); 110 bytes per entity (baseline 232), create 101 ns, destroy 40 ns;
      stamping +9.1 percent; every pass condition holds. `spatial_tests "[spatial-bench]"`: moving 100,000 agents 171 ms a frame in a tree and
      34 ms in a grid; culling gain 76x at 100k and 53x at 1M; the sync of 100,000 moving agents 322 ms on 1 worker, 174 on 2, 173 on 4, 175 on 8;
      the margin sweep 524 ms at margin 0 down to 80 ms at margin 1. Recorded in `docs/guide/ecs.md` and `spatial.md`.)
- [x] **Documentation:** the guides of each module note "runs on macOS" where they list platforms; `docgen lint` zero; reference regenerated if any
      header changed. (`ui.md` has an "On macOS" section; `ecs.md` and `spatial.md` have the Mac's numbers; `text.md`, `database.md` and
      `storage.md` list no platforms, so they say nothing. No header changed today; `docs/reference/symbols.txt` is regenerated anyway, because
      Day 293's formatting commit `fd4c65a8` rewrapped a comment line in `settings/settings.h` without regenerating, which `docs_check` found
      in today's run.)
- [x] **Runs:** `scripts/test-phase.sh --affected --flavor all` (quote).
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: any code change made for libc++ or Apple clang staying correct on Linux and MSVC.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** text, UI (CPU), ECS, spatial, database and storage, and any other window-free module on `master` except `net` (Day 310), build and pass their suites on macOS in three flavors and both unity
modes, with byte-identical text outputs to Linux and the benchmark numbers recorded.

## Result

Done on 2026-11-12 on the Mac. Text (with FreeType), UI (CPU side), ECS, spatial, the database (with SQLite) and storage build at the first attempt on
macOS and pass their suites in plain, asan and tsan, in both unity modes, with the text atlas byte-identical to Linux's goldens. No code changed for
Apple clang or libc++: the day's changes are CMake (the modules added to the macOS block, `platform_input` before the window layer, `tests/ui`'s
window-bound tests held back, SQLite's TSan suppressions on macOS), the guides and the phase map. `engine/net` moved to its own day (Day 310; plan
change committed first, `2fc956af`).

- **Runs (macOS, the script):** `--affected --flavor all`: `ran 4114 of 4493 tests (phases: all; flavors: all; display: included; crash tests:
  excluded) in 1395 s`, two failures: `Throughput scales with participant count` (flaky under load, `KNOWN-ISSUES.md`) and `docs_check` (the stale
  `symbols.txt`, regenerated: `ctest -L docs` 3 of 3). Non-unity, the day's phases one by one, all passed:
  `ran 150 of 4493 tests (phases: 14; ...)`, `ran 852 of 4493 tests (phases: 15; ...)`, `ran 378 of 4493 tests (phases: 17; ...)`, `ran 231 of 4493 tests
  (phases: 18; ...)`, `ran 804 of 4493 tests (phases: 19; ...)` (each `flavors: all; display: included; crash tests: excluded`).

## Post-implementation code review

`/code-review high` on the day's uncommitted changes: 8 findings, all fixed or answered; none a run-time bug.

1. **Fixed: `tests/ui` held the window tests back while there was no `platform` target**, but they need `ui_window`, which the macOS block adds only
   with the window layer: on Day 295 the tests would have come back with nothing to link. The condition is `NOT TARGET ui_window`.
2. **Fixed: the regenerated `symbols.txt` was not recorded** (Day 293's formatting commit had left it stale): the day file and the shared-files
   entry of `KNOWN-ISSUES.md` name it now.
3. **Fixed: the Documentation task said no regeneration was needed**: corrected with (2).
4. **`STATUS.md` was not yet in the diff:** updated in this commit.
5. **Fixed: `platform_input` was defined twice** (once for macOS, once after the branches): defined once before them; macOS's branch only returns.
6. **Fixed: the spatial guide's macOS figures ran into the paragraph before them**: a blank line.
7. **Checked, no change: a duplicate-library linker warning for today's programs.** Relinked `ecs_bench` and the six test programs in both trees: no
   warning; only `io_tests` and `debug_tests` print it, as recorded on Day 293.
8. **Fixed: the Phase 15 flavor line credited only `tests/foundation`**: it names what gives each flavor.
