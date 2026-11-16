# Phase 25 / Day 395 — Determinism II on Linux: Hashes, Threads and Flavors

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 394.

Establish the reference hashes the other two machines must match.

## Tasks

- [ ] Reference scenes with committed hashes (`tests/physics/golden/*.hash`): a 10-box stack, a 5x5 pyramid, a 2000-body mixed pile with hulls and a mesh floor, a hinge chain bridge, a character walking a course, a bullet and a wall; each runs 600 steps (and the long ones 3600) and prints the hash of the final state and every 100th step.
- [ ] The same hashes for: 1, 2, 8, 32 threads; GCC and Clang; unity and non-unity; plain, asan and tsan flavors; `-O0` and `-O2` (a debug and a release tree).
- [ ] A `physics_determinism` ctest running the scenes and comparing to the committed hashes with a clear diff report (first differing step, first differing body, the field); an `ENGINE_UPDATE_GOLDEN=1` style update path as the other golden tests.
- [ ] Where a hash differs between configurations, find the cause (a non-exact operation, an unordered merge, an uninitialised field) and fix it in the step; a cause that is libm (`Atan2` in the hinge) is listed in `KNOWN-ISSUES.md` with the configurations it differs in, not worked around by new math (user decision).
- [ ] Cost per body: steps per second for 1k, 5k, 20k bodies awake and asleep, recorded in the day file as the baseline for Day 400.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Six reference scenes have committed hashes that every Linux configuration reproduces, and anything that does not is explained in `KNOWN-ISSUES.md`.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
