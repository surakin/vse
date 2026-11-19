# Shorter test runs / Day 1: the build step: parallel, and only the chosen flavors

**Series:** [shorter test runs](../test-time.md)
**Depends on:** Phase 6 (`scripts/test-phase.sh`), the sanitizer flavors (one tree, `_asan` and `_tsan` targets).

## Tasks

- [x] **Measure first** (Linux, the `build` tree; record each number in the Notes):
  - a no-op `cmake --build build` (nothing changed);
  - the unlit-sized rebuild: touch `engine/renderer/gbuffer.h`, then build;
  - each of these with no `-j` and with `-j32`;
  - while the build runs, check whether it really ran one job at a time (the job count with `ps`).
- [x] **A parallel build step:**
  - `scripts/test-phase.sh` passes `--parallel "$jobs"` to `cmake --build`;
  - `--jobs` now means both the build jobs and the test jobs;
  - an explicit `CMAKE_BUILD_PARALLEL_LEVEL` in the environment wins;
  - works under the Mac's bash 3.2 and Git Bash.
- [x] **Build only the chosen flavors:**
  - three aggregate targets, `engine_tests_plain`, `engine_tests_asan` and `engine_tests_tsan`;
  - each depends on every test executable, gate and demo that a ctest of that flavor runs;
  - they are collected where `engine_label_tests` and the sanitizer helpers already see each target, so a new test joins without a new line;
  - the script builds the chosen flavors' targets: plain builds no `_asan` or `_tsan` object, and `--flavor all` builds all three;
  - `--all` and a `docs` run keep building `all`.
- [x] **A test that no target builds is an error, not a silent stale binary:**
  - a check that every ctest command's executable is a dependency of its flavor's aggregate target;
  - it lives in the existing script tests (`test_phase_script_*`), with a fixture that leaves one out and fails.
- [x] **Ninja, measured:**
  - configure a second tree with `-G Ninja` and repeat the measurements above;
  - if a no-op build or the rebuild is shorter: add `ninja` to `BUILD.md` (for checks, optional) and a `linux-ninja` preset;
    (done differently, see the Notes: `BUILD.md` already lists Ninja as required on Linux, and Windows and the Mac already use it);
  - the default stays Makefiles unless the user says otherwise;
  - if neither is shorter: record the numbers and drop it.
- [x] **sccache, measured:**
  - set it as `CMAKE_CXX_COMPILER_LAUNCHER` in a scratch tree;
  - measure a clean build twice (cold, then warm) and the rebuild;
  - adopt it the same way as Ninja, only if it helps; it is never required.
- [x] **Docs:** `docs/guide/building-and-testing.md` ("Test phases and scoped runs"): the build step's parallelism and the per-flavor targets.
- [x] **Standing rows:**
  - `scripts/test-phase.sh --affected` (plain), with its `ran X of Y` line;
  - the script tests;
  - Clang unity and GCC non-unity builds;
  - `/code-review high`;
  - `STATUS.md`, `KNOWN-ISSUES.md` (the PC and the Mac have not run the new script);
  - commit and push.

**Done when:**
- `scripts/test-phase.sh --affected` builds in parallel and builds only the chosen flavors;
- the before and after build times are recorded in the Notes;
- a test whose executable no flavor target builds fails the script tests.

## Notes

### What was built (2026-11-19)

- **`cmake/EngineFlavorTargets.cmake`**, three build targets:
  - `engine_tests_plain`, `engine_tests_asan` and `engine_tests_tsan`, made at the end of the root `CMakeLists.txt` from every
    non-excluded target outside `third_party/`;
  - a target's flavor comes from its name: `asan` or `tsan` as a word (`ecs_tests_asan`, `tsan_canary`);
  - `<build>/engine_flavor_targets.txt` lists each executable with its flavor and file.
- **Helpers:** `engine_flavor_needs(<flavor> <target>...)` adds another flavor's program to a flavor's target; no test needs it yet.
  `engine_flavor_label` gives a test the flavor of the program it inspects.
- **`scripts/test-phase.sh`:**
  - builds the chosen flavors' targets with `--parallel $jobs`; `CMAKE_BUILD_PARALLEL_LEVEL`, if set, wins;
  - re-runs CMake once in a tree configured before the targets existed;
  - one departure from the plan: `--all` builds the flavor targets too, not `all`. Together the three are every target of ours, so
    `--all --flavor all` builds the same set as before, and `--all --flavor plain` builds less.
- **The check** `test_phase_script_flavor_targets` (`tests/test_phase_script/flavor_targets.cmake`):
  - it reads `ctest -N -V`. Every word of a test's command that is one of our executables must be in the test's flavor;
  - a program in the build tree that is no target's file is reported;
  - it takes 0.9 s over 11,455 tests;
  - `test_phase_script_flavor_targets_planted` fails on a fixture with one of each.
- **Found by the check, fixed:**
  - `tsan_canary` was plain by the first naming rule (a suffix); the rule is now "a word of the name";
  - `debug_no_network_debug_tests_asan` and `_tsan` were labelled plain but inspect sanitized programs: they now carry their
    program's flavor (Linux, Windows and macOS lists);
  - `debug_stack_trace_stripped` runs `debug_tests.stripped`, a copy that `debug_tests`' POST_BUILD step makes: the check reads it as
    `debug_tests`.

### Measurements (Linux, 32 CPUs, Release, 2026-11-19)

| Build | Time |
|---|---|
| no-op, no `-j` / `-j32` (Make) | 5.5 s / 0.4 s |
| `gbuffer.h` touched, every flavor, no `-j` (what the script did) | **356.6 s** |
| `gbuffer.h` touched, every flavor, `-j32` | 44.5 s |
| `gbuffer.h` touched, plain only (`engine_tests_plain`), `-j32` | 21.0 s |

- **The script built one job at a time:** while the serial build ran there was one compiler process (a clang driver and its `-cc1`).

**Ninja** (a scratch tree, the same options):

| | Ninja | Make |
|---|---|---|
| full build, `-j32` | 87.1 s | |
| no-op, plain target | 0.05 s | 0.45 s |
| `gbuffer.h` touched, plain target | 12.1 s | 20.7 s |
| the rest of the flavors after that | 29.8 s | 42.9 s |

- Windows and the Mac already use Ninja (their presets), and `BUILD.md` lists "Ninja or Make" as required on Linux.
- The `linux` preset and the `build` tree stay on Makefiles: switching means re-creating the user's tree, so it is their call.
  `BUILD.md`'s Linux row now gives the numbers.

**sccache** (a scratch tree and a scratch local cache):
- The user's sccache is set up for a network cache that refused this session's requests, so the measurement used a local cache in a
  scratch directory, with the user's configuration left alone.
- Ninja full build: cold cache 86.9 s, the same as without it. A new tree with a warm cache: 10.4 s (876 of 1,752 compiles were hits,
  the second build's).
- It cannot help a rebuild after an edit (the edited inputs miss), so it is listed as optional in `BUILD.md`, not adopted.
- A first attempt in `/tmp` failed: the scratch trees (7 GB) filled the tmpfs ("No space left on device"); it was redone on the main disk,
  and the scratch trees were deleted.

### Runs

- `scripts/test-phase.sh --affected` (plain): `ran 4264 of 11457 tests (phases: all; flavors: plain; display: included; crash tests:
  excluded) in 397 s`; the build step took about 13 s and built only `engine_tests_plain`.
  - The run still covered every phase, because `--affected` diffs against the `CURRENT_PHASE` commit. Day 2 fixes that.
  - Failures, all known (`KNOWN-ISSUES.md`): `script_bench_check` (timing under load), `engine_c_surface`, the Job-destroyed test, the
    voice-cost snapshot, and the DXIL and Metal staleness checks left by the unlit change.
- After the review's fixes: every phase 6 test passes (62 of 62, the new build-plan and flavor-target tests included), as do the 14
  relabelled no-network checks and the stripped-binary test. `scripts/format.sh --check` is clean. The full `--affected` run was not
  repeated, since the fixes touch only the script, the flavor-target CMake and the script tests.
- `ctest -L docs`: 9 of 9 (the guide changed).
- Builds: Clang unity (`build`) and GCC non-unity (`build-gcc`), both clean; `engine_tests_plain` builds in both.

### Post-implementation code review

`/code-review high`: 5 findings, all fixed.
1. The source-dir prefix test in `EngineFlavorTargets.cmake` was a regex with the unescaped path; it is now a `string(FIND)` prefix test.
2. The same for the build-dir test in the check.
3. The script re-ran CMake only when `engine_flavor_targets.txt` was missing. A tree reconfigured from an older checkout keeps that
   file but has no rule for the targets. The script now asks the generated `Makefile` or `build.ninja` for `engine_tests_plain`.
4. Nothing tested the build step: the script tests all use `--dry-run`, which skips it.
   - `--dry-run` now prints the build step it would run;
   - `test_phase_script_build_plan_{plain,asan,all,env}` check the targets, `--jobs`, and that `CMAKE_BUILD_PARALLEL_LEVEL` drops
     `--parallel`;
   - with tsan dropped from `all` and the environment ignored, the `all` and `env` cases fail (checked, then restored).
5. The check special-cased a `.stripped` suffix. A target now lists such a copy in `ENGINE_FLAVOR_EXTRA_FILES` (`debug_tests` does),
   and the suffix rule is gone.
