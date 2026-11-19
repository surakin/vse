# Shorter test runs / Day 5: the PC and the Mac (the user's machines)

**Series:** [shorter test runs](../test-time.md)
**Depends on:** Days 1 to 4.

## Tasks

- [ ] **The PC** (Git Bash, `build-msvc`):
  - `scripts/test-phase.sh --start-day`, then `--affected`, then `--soak`;
  - the per-flavor build targets work with the MSVC tree's flavors (plain and asan; no tsan);
  - the parallel build flag reaches MSBuild or Ninja, whichever the preset uses;
  - record the before and after times of an `--affected` run.
- [x] **The Mac** (`/bin/bash` 3.2, `build-macos`; done 2026-11-19):
  - the same three runs;
  - the script uses no bash 4 feature: the script tests run there;
  - per the standing rule, unity trees only and plain flavor only.
- [ ] **The short forms' counts** (the Mac's half done, 2026-11-19; the PC's open): on each machine, check that no ordinary test is over the 30 s bar, and record any that is. A slower
  machine may need its own count: a setting, not a different test.
- [ ] **Standing rows:**
  - the `ran X of Y` lines on each machine;
  - `STATUS.md`;
  - `KNOWN-ISSUES.md`: remove the "not run on the PC and the Mac" entries this series added;
  - commit and push.

**Done when** the new script, the `soak` label and the per-flavor build run on the PC and the Mac, and their before and after times are
recorded.

## Notes

### The Mac (2026-11-19)

The Mac: 8 CPUs, `build-macos` (Ninja, Debug, unity), `/bin/bash` 3.2.57 and the system's `awk`. Plain flavor only (the standing rule).
The console was locked, so every run here left the window tests out.

**Found: the per-flavor targets did not exist on macOS (fixed).**
- The root `CMakeLists.txt`'s macOS branch, and its Windows branch, end in `return()` before the file's last line, which calls
  `engine_add_flavor_targets()`.
  - So there was no `engine_tests_plain` here: the script's build step stopped with `ninja: error: unknown target 'engine_tests_plain'`.
  - There was no `engine_flavor_targets.txt` either, so `test_phase_script_flavor_targets` failed ("configure the build first").
- Fixed first with a call before each branch's `return()`, then, after the review, with one call:
  `cmake/EngineFlavorTargets.cmake` defers `engine_add_flavor_targets()` to the end of the top-level directory
  (`cmake_language(DEFER DIRECTORY ...)`), however that directory ends. The root file's explicit call is gone.
  - On the Mac, `engine_flavor_targets.txt` lists 212 lines, `build.ninja` has the `engine_tests_plain` rule, and all 54
    `test_phase_script_*` ctests pass (`_flavor_targets` and its planted fixture included).
  - The Windows branch gets the targets the same way. No Windows configure has run it yet.

**The script under bash 3.2:**
- Every `test_phase_script_*` ctest passes under `/bin/bash` 3.2 (CMake's `BASH_PROGRAM` is `/bin/bash`), including `test_phase_script_base`
  (the base rules, in a scratch repository and a worktree) and `test_phase_script_long_warning_bwk_awk` (the long-test warning under the
  Mac's awk).
- `test_phase_labels`, `platform_neutral_check` and `file_access_check` (Day 4's rewrites) pass.
- `--start-day ef686687` wrote `.git/test-phase-base`. The `--affected` run's first line names it: "compares with ef686687728e: the day
  start recorded by --start-day". It ran everything, because `CMakeLists.txt`, `cmake/` and `crash_handler.h` changed since that commit.
- `metal_autorelease_pools` (Day 4 set `ENGINE_TEST_SOAK=1` for it) passed in 6.0 s.

**Before and after** (`--affected --crashes --no-display`, every phase both times, because `cmake/` changed in both diffs):

| | Build | Tests | ctest wall time |
|---|---|---|---|
| Before (old script, earlier on 2026-11-19, before Days 1-4 were pulled) | `cmake --build build-macos`, every flavor | 4,139 | 946 s |
| After (`9eb2a610` plus this day's fix) | `--parallel 8 --target engine_tests_plain`: no work | 4,163 | 712 s |

- The after run took 11 min 54 s in all, the no-op build included.
  `ran 4163 of 11291 tests (phases: all; flavors: plain; display: excluded; crash tests: included; soak: excluded) in 712 s`.
- One failure in each run: the known `Snapshot: the cost of a voice at each quality, with and without effects`.
- A full build of every flavor after the pull took 187 s (the before route builds that each time the tree is stale). The script's build of
  `engine_tests_plain` compiles no `_asan` or `_tsan` object.

**`--soak`:** on this Debug tree the soak label holds 4 tests. The three `*_large` benches are added only in Release and RelWithDebInfo
builds. The two hostile `[asan]` and `[tsan]` soak forms are not run here (plain only).
- `scripts/test-phase.sh 8 --soak --no-display --no-build`: `ran 121 of 11291 tests (phases: 8; flavors: plain; display: excluded; crash
  tests: excluded; soak: included) in 15 s`, no failures. The reset loop: 3.1 s for 20 resets, 7.2 s for 100 in its soak form.
- `scripts/test-phase.sh 27 --soak --no-display --no-build`: `ran 317 of 11291 tests (phases: 27; flavors: plain; display: excluded; crash
  tests: excluded; soak: included) in 249 s`. The hostile soak form passed in 60.3 s.
  - The short hostile form failed, at `script_hostile_test.cpp:130` (`CHECK(ticks >= 3)`). Run again twice with
    `CTEST_OUTPUT_ON_FAILURE=1`, it failed both times, with `ticks 2, errors 45, distinct 21`: 3 of 3 `27 --soak` runs.
    - It passed in the `--affected` run (10.7 s), which left the soak form out, and 3 of 3 alone (10.2 s; 59 ticks).
    - Recorded under failing tests in `KNOWN-ISSUES.md`.

**Tests over the 30 s bar on the Mac.** The `--affected` run's warning listed 32 tests, run 8 at a time on 8 CPUs. Measured alone, plain:

| Seconds alone | Test |
|---|---|
| 110.9 | `script_demo_screenshot` |
| 76.8 | HashGrid: 100,000 random operations against a flat model |
| 68.3 | `shaders_check` |
| 55.2 | `docs_samples` |
| 50.3 | `engine_c_script_sample` |
| 49.6 | `shared_unit_check` |
| 46.3 | Golden: every scene reproduces its committed hashes for every thread count |
| 38.5 | `net_access_check` |
| 37.3 | Spatial sync: the cost for 100,000 entities ... is recorded |
| 29.0 / 28.8 / 26.7 | Script demo state hash at 1, 2 and 16 threads / `raw_new_check` / `sqlite_boundary_check` |

- Nine plain tests are over 30 s alone. Both presets are Debug, so this machine is about 2 to 4 times slower per test than the Linux
  machine (`shaders_check`: 68.3 s against 15.7 s).
- No Mac-specific count was added. Per the plan, that would be a setting, and it is the user's call (`KNOWN-ISSUES.md`).
- By mistake, the alone measurement first also ran HashGrid's `[asan]` and `[tsan]` forms (288 s and 626 s, both passed): a sanitizer run
  on the Mac, against the standing rule. They are not counted.

### Post-implementation code review

`/code-review high` on the Mac half's diff: 7 findings; 5 fixed, 1 recorded, 1 declined.
1. **The `KNOWN-ISSUES.md` entry contradicted the day file.** It still listed Mac items as not run. Fixed: it is rewritten as (1) what the
   PC has to run, (2) what passed on the Mac and (3) the Linux observations.
2. **Three call sites, each easy to forget on the next platform branch** (the bug this day found). Fixed:
   - `cmake/EngineFlavorTargets.cmake` defers `engine_add_flavor_targets()` to the end of the top-level directory
     (`cmake_language(DEFER DIRECTORY ...)`, CMake 3.19; the project needs 3.21);
   - the root file's explicit call and the two branch calls are gone;
   - reconfigured on the Mac: 212 lines in `engine_flavor_targets.txt`, the `engine_tests_plain` rule, and 54 of 54 `test_phase_script_*`
     ctests pass.
3. **The Windows branch has never been configured with the change.** Recorded: it is item (1) of the `KNOWN-ISSUES.md` entry, for the PC's
   half.
4. **The hostile-scripts failure was filed as flaky with its output lost.** Fixed:
   - run again with the output kept, it failed 3 of 3 under `27 --soak`, with `ticks 2` and 59 ticks alone;
   - it moved to failing tests, with the numbers.
   - A lock between the two forms was declined. Only `RUN_SERIAL` could do it, since the short form is a discovered test, which would add
     the soak forms' minutes to every `--all` run and hide the stall.
5. **The day file pointed to a review section that did not exist.** Fixed: this section, and `STATUS.md` mentions it.
6. **`scripts/test-phase.sh` gave only ninja's "unknown target" when the targets were missing.** Fixed: after CMake runs again, the
   script checks the generated files once more and names the cause. Checked against a scratch Ninja tree with no engine targets: the
   message appears and nothing runs.
7. **No regression guard.** Declined as a separate check: the deferred call (2) leaves no platform branch to forget, and the script's
   message (6) names the cause if a configuration still lacks the targets.

