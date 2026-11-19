# Shorter test runs / Day 1: the build step: parallel, and only the chosen flavors

**Series:** [shorter test runs](../test-time.md)
**Depends on:** Phase 6 (`scripts/test-phase.sh`), the sanitizer flavors (one tree, `_asan` and `_tsan` targets).

## Tasks

- [ ] **Measure first** (Linux, the `build` tree; record each number in the Notes):
  - a no-op `cmake --build build` (nothing changed);
  - the unlit-sized rebuild: touch `engine/renderer/gbuffer.h`, then build;
  - each of these with no `-j` and with `-j32`;
  - while the build runs, check whether it really ran one job at a time (the job count with `ps`).
- [ ] **A parallel build step:**
  - `scripts/test-phase.sh` passes `--parallel "$jobs"` to `cmake --build`;
  - `--jobs` now means both the build jobs and the test jobs;
  - an explicit `CMAKE_BUILD_PARALLEL_LEVEL` in the environment wins;
  - works under the Mac's bash 3.2 and Git Bash.
- [ ] **Build only the chosen flavors:**
  - three aggregate targets, `engine_tests_plain`, `engine_tests_asan` and `engine_tests_tsan`;
  - each depends on every test executable, gate and demo that a ctest of that flavor runs;
  - they are collected where `engine_label_tests` and the sanitizer helpers already see each target, so a new test joins without a new line;
  - the script builds the chosen flavors' targets: plain builds no `_asan` or `_tsan` object, and `--flavor all` builds all three;
  - `--all` and a `docs` run keep building `all`.
- [ ] **A test that no target builds is an error, not a silent stale binary:**
  - a check that every ctest command's executable is a dependency of its flavor's aggregate target;
  - it lives in the existing script tests (`test_phase_script_*`), with a fixture that leaves one out and fails.
- [ ] **Ninja, measured:**
  - configure a second tree with `-G Ninja` and repeat the measurements above;
  - if a no-op build or the rebuild is shorter: add `ninja` to `BUILD.md` (for checks, optional) and a `linux-ninja` preset;
  - the default stays Makefiles unless the user says otherwise;
  - if neither is shorter: record the numbers and drop it.
- [ ] **sccache, measured:**
  - set it as `CMAKE_CXX_COMPILER_LAUNCHER` in a scratch tree;
  - measure a clean build twice (cold, then warm) and the rebuild;
  - adopt it the same way as Ninja, only if it helps; it is never required.
- [ ] **Docs:** `docs/guide/building-and-testing.md` ("Test phases and scoped runs"): the build step's parallelism and the per-flavor targets.
- [ ] **Standing rows:**
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
