# Phase 25 / Day 377 — The Broad Phase on Phase 18's Spatial Module

**Phase:** [Phase 25 — Physics](../phase-25-physics.md)
**Depends on:** Day 376; Phase 18 (`spatial::DynamicBvh`, `StaticBvh`, `SpatialSet`, `PairTracker`).

Find which pairs of colliders may touch, using the structures that already exist; nothing new is built here but the glue and its proof.

## Tasks

- [ ] A physics `SpatialSet` pair registered with the world's `SpatialWorld`: a static set backed by `StaticBvh` (rebuilt when static colliders change, in a batch) and a dynamic set backed by `DynamicBvh` with fat boxes (the margin from the Day 373 spec); every collider's key is its `ColliderId`.
- [ ] Bounds come from the shape and the body transform; a body moving faster than its fat margin is re-inserted, others cost nothing (the sync system of Phase 18 reports what moved); swept bounds (the box extended along the velocity for one step) are used for bodies flagged `continuous`.
- [ ] The layer matrix: `physics::CollisionFilter` (32 layers, `meets` bits) is the `spatial::PairTrackerConfig` of the physics set, plus per-collider `group` and `mask` words for finer filtering after the broad phase; static-static pairs never form.
- [ ] The pair feed: begin and end events from `PairTracker` (sorted by key, Phase 18's guarantee) become the narrow phase's deterministic work list: a vector of `(ColliderA, ColliderB)` with the smaller id first, plus the kept pairs from earlier steps, in sorted order.
- [ ] Triggers join the same feed and are flagged; sensors never reach the solver.
- [ ] Statistics: pairs begun, ended and kept, nodes visited, rebuilds, per step, exposed as `PhysicsStats`.
- [ ] An oracle test: a brute-force O(n^2) overlap of the same boxes gives the same pair set on 20 random scenes of 1000 bodies; the pair list is identical for 1, 2 and 8 job threads; a body that jitters at the edge of contact does not begin and end every step (the margin works).
- [ ] A test that fails when the static tree is not rebuilt after a static collider is added, removed or moved: `SpatialSet::Stats().pending_static` is zero before the pair update, and the update of 5000 moving boxes with 2000 statics stays under 3 times the figure with the tree built (Day 373 spike 6: an unbuilt tree cost 31 ms against 4 ms a frame, every query scanning the pending statics).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase is concurrency, lifetime and memory code on nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, flags, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 398-399, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/physics.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 25); only wrapped types, no `std`, no raw platform macros.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Moving bodies produce the same sorted pair work list as a brute-force search, independent of the thread count, at a cost the stats report.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
