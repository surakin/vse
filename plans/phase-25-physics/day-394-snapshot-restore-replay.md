# Phase 25 / Day 394 — Snapshot, Restore and Replay

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 393; Phase 19 (`engine/storage`).

Save and load the whole simulation, and prove the step is a pure function of its state.

## Tasks

- [ ] `PhysicsWorld::Snapshot()` and `Restore()` through the storage interface (Phase 19; no private format): every body, collider, joint, contact cache entry, sleeping state, accumulator and stats-independent counters, in a versioned, endian-fixed layout; restore reproduces the exact state (the state hash equals).
- [ ] Rollback: `Restore` then re-run N steps gives the same hash as the original run (the replay test); a snapshot taken mid-contact restores the warm-start caches so the next step is identical.
- [ ] Input recording for replay: applied forces and impulses, teleports and creations are recorded with the step number into an `InputLog`; replaying the log from a snapshot reproduces the run.
- [ ] Forward compatibility: the snapshot carries a version, an unknown version is refused with a `Status`, truncated or corrupt data is refused without a crash (hostile-input rules, a fuzz-style test of 10^4 mutated snapshots).
- [ ] Snapshot size and time are recorded for the 2000-body scene.
- [ ] Tests: snapshot/restore equality of the state hash at a dozen points of a stack scene and a joint scene; replay equality over 600 steps with a log of impulses; corrupt-input refusal; restoring into a world with different content replaces it entirely.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A run restored from a snapshot and a replayed input log reproduces the original state hash, and corrupt snapshots are refused.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
