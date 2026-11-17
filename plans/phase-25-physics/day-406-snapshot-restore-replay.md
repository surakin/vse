# Phase 25 / Day 406 — Snapshot, Restore and Replay

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 405; Phase 19 (`engine/storage`).

Save and load the whole simulation, and prove the step is a pure function of its state.

## Tasks

- [x] `PhysicsWorld::Snapshot()` and `Restore()` through the storage interface (Phase 19; no private format): every body, collider, joint, contact cache entry, sleeping state, accumulator and stats-independent counters, in a versioned, endian-fixed layout; restore reproduces the exact state (the state hash equals).
- [x] Rollback: `Restore` then re-run N steps gives the same hash as the original run (the replay test); a snapshot taken mid-contact restores the warm-start caches so the next step is identical.
- [x] Input recording for replay: applied forces and impulses, teleports and creations are recorded with the step number into an `InputLog`; replaying the log from a snapshot reproduces the run.
- [x] Forward compatibility: the snapshot carries a version, an unknown version is refused with a `Status`, truncated or corrupt data is refused without a crash (hostile-input rules, a fuzz-style test of 10^4 mutated snapshots).
- [x] Snapshot size and time are recorded for the 2000-body scene.
- [x] Tests: snapshot/restore equality of the state hash at a dozen points of a stack scene and a joint scene; replay equality over 600 steps with a log of impulses; corrupt-input refusal; restoring into a world with different content replaces it entirely.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 410-411, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A run restored from a snapshot and a replayed input log reproduces the original state hash, and corrupt snapshots are refused.

## Outcome

Snapshot/Restore of the whole world through `bytes::ByteWriter`/`ByteReader` (canonical little-endian, versioned, magic PHSN), `SaveSnapshot`/`LoadSnapshot` as one storage entity record, `InputLog`/`InputRecorder`/`ReplayStep`. Restore builds a fresh world, validates everything and swaps atomically; libraries are content (LibraryMismatch when a shape is unknown); mass is recomputed, not stored. Restore at a dozen points continues bit for bit; 600-step replay from a snapshot with a saved/loaded log matches; 10^4 mutated snapshots (9 seeds checked) are refused or restore to a world that steps safely. The mutation test found five gaps in Validate (denormal mass, density, hertz, joint anchors and lengths, collider offsets) now bounded for hosts too (kMinMass/kMaxMass, kMaxExtent). 2000-body scene: 772,571 bytes, snapshot 17.8 ms, restore 5.4 ms. Runs: plain ran 3600 of 9660 tests (phases: all; flavors: plain), tsan ran 2568 of 9660, asan ran 2980 of 9660; all 100% passed (a reduced run, not the full suite). Four trees: 205 physics cases pass. Docs: docs/guide/physics.md (Snapshot, restore and replay), docs target clean.

## Post-implementation code review

Review (high): 6 findings. Fixed: Restore's mass bound was tighter than CreateBody's (a 5e-7 kg body could not be restored; regression test added); Validate gaps that produced NaN (found by the mutation test, regression test added); guide omitted what is not saved; Truncated reported for non-truncation failures. Declined: const_cast in SlotPool::SlotGeneration (same pattern as LiveGeneration); replay of extreme inputs is trusted like direct API calls (test shows the world stays finite).
