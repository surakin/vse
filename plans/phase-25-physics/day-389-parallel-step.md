# Phase 25 / Day 389 — The Parallel Step on the Job System

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 388; Phase 2 (jobs).

Use all cores without changing a single result bit.

## Tasks

- [ ] Parallel narrow phase: the sorted pair work list is split into fixed-size blocks, each block writes its manifolds into a preallocated slot range (no shared appends), and the blocks are concatenated by index; a job count change cannot change the output.
- [ ] Parallel solve: islands are independent, so each island is a job (or a group of small islands is one job); islands are scheduled in id order and each writes only its own bodies and rows; a huge island is solved by one job (the spec records this limit, graph colouring is not built).
- [ ] Parallel integration and bounds updates over body ranges in fixed chunks; the broad-phase sync of Phase 18 already parallelises its own work.
- [ ] No float reduction in thread order: any sum across jobs is done on ordered partial results, in index order, after the jobs finish; no atomics on floats; per-job scratch memory from the frame arena.
- [ ] Everything goes through `jobs/` (the one scheduler, never an ad hoc thread pool); the step uses the coroutine job API as the other engine systems do; a world with `physics.threads` of 1 runs inline.
- [ ] Tests: the state hash after 600 steps of three scenes (a stack field, a pile of 2000 mixed shapes, a hinge chain world) is identical for 1, 2, 4, 8 and 32 threads and for shuffled job start orders (the scheduler's test hook); tsan flavor runs the scenes with no race; a scaling benchmark (steps per second against threads) recorded in the day file.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The step is identical to the bit for every thread count, tsan is clean, and the speed-up over one thread is measured.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
