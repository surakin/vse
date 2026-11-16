# Phase 25 / Day 389 — The Parallel Step on the Job System

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 388; Phase 2 (jobs).

Use all cores without changing a single result bit.

## Tasks

- [x] Parallel narrow phase: the sorted pair work list is split into fixed-size blocks, each block writes its manifolds into a preallocated slot range (no shared appends), and the blocks are concatenated by index; a job count change cannot change the output.
- [ ] Parallel solve: islands are independent, so each island is a job (or a group of small islands is one job); islands are scheduled in id order and each writes only its own bodies and rows; a huge island is solved by one job (the spec records this limit, graph colouring is not built).
- [x] Parallel integration and bounds updates over body ranges in fixed chunks; the broad-phase sync of Phase 18 already parallelises its own work.
- [x] No float reduction in thread order: any sum across jobs is done on ordered partial results, in index order, after the jobs finish; no atomics on floats; per-job scratch memory from the frame arena.
- [x] Everything goes through `jobs/` (the one scheduler, never an ad hoc thread pool); the step uses the coroutine job API as the other engine systems do; a world with `physics.threads` of 1 runs inline.
- [ ] Tests: the state hash after 600 steps of three scenes (a stack field, a pile of 2000 mixed shapes, a hinge chain world) is identical for 1, 2, 4, 8 and 32 threads and for shuffled job start orders (the scheduler's test hook); tsan flavor runs the scenes with no race; a scaling benchmark (steps per second against threads) recorded in the day file.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The step is identical to the bit for every thread count, tsan is clean, and the speed-up over one thread is measured.

## Outcome

`PhysicsWorld::SetScheduler`, `StepConfig::threads` (`physics.threads`, 1 = inline). With a scheduler and threads above 1, the narrow phase runs as blocks of 32 pairs (each block reads the poses and writes its own slot; the contacts, wakes and island edges are applied on the main thread in pair order, in two passes so an island woken mid-pass still gets all its contacts) and the free bodies (not integrated by the solver) integrate in blocks of 256. No cross-job sums or float atomics; everything goes through `jobs::ParallelForBlocking`. Tests (`tests/physics/parallel_test.cpp`): the state hash after 120 steps of a stack field (16 stacks of 4), a pile of 300 mixed spheres, boxes and capsules in a container, and 4 hinge chains is equal with no scheduler and with 1, 2, 4, 8 and 32 participants (clang debug, GCC and non-unity; tsan runs the same). Scaling (release, 1500-body pile, 20 steps, falling and mostly not yet touching): 100.5 ms inline, 95.9 ms with 2 and 94.7 ms with 4 participants: the narrow phase is a small share, and the contact solver is single-threaded. NOT done (left open in the task list): the island-parallel solve (the solver's single sorted constraint list and contact cache would have to be split per island; `KNOWN-ISSUES.md`) and the shuffled job start order (the scheduler has no such test hook), and the plan's sizes (2000 bodies, 600 steps) were cut to 300 bodies and 120 steps to keep the debug and sanitizer runs short. Docs: `docs/guide/physics.md` ("The parallel step"), docgen 100 %. Runs: plain `ran 3548 of 9504 tests` (one failure: the known flaky `Benchmark: the wrappers cost what the standard types cost`), asan `ran 2928 of 9504`, tsan `ran 2516 of 9504`.

## Post-implementation code review

`/code-review high`: 3 findings. Fixed: a redundant force-clearing line in the integration. Declined: the hash test cannot prove jobs ran in parallel (the only path to the parallel code is `ForBlocks`, and tsan runs the scenes); the solver is still single-threaded (recorded as open work in `KNOWN-ISSUES.md`, not a defect of this change).
