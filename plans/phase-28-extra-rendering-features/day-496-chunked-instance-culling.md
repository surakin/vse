# Phase 28 / Day 496 — Chunked Instance Culling on the CPU

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Day 495.

Cull a million instances cheaply: whole chunks first, instances inside a boundary chunk second.

## Tasks

- [x] `CullInstances(set, frustum, lod_params, out)`: the chunk bounds go through the frustum test (a fully inside chunk emits all its instances without a per-instance test; a partly inside one tests each; a fully outside one is skipped); the chunk tests are spread over the job scheduler (`jobs`), one job per group of chunks, with the per-job output lists concatenated in chunk order so the visible list is identical for any thread count.
- [x] A distance cull and an optional maximum draw distance per set; a small-instance cull (projected size below a pixel threshold) as a setting; a visible-instance list holds `(chunk, first, count)` runs, not one entry per instance, when a chunk is fully visible.
- [x] The spatial module: the set's chunk bounds are inserted into a `spatial` static or dynamic structure so a ray or overlap query finds instances (an `InstanceQuery`: which instance does this ray hit by bounds), and the sync follows `spatial_sync` rules, not a private grid.
- [x] The renderer's existing frustum type is reused (`renderer::Frustum`); no second frustum code.
- [x] Counters for the cull (chunks tested, chunks skipped, instances tested, instances emitted) exposed in `RendererInfo` and the overlay, so a user sees the cost.
- [x] Tests: a brute-force per-instance frustum test equals the chunked result as a set on 100,000 random instances and 200 random cameras; the result is identical at 1 and 8 threads; fully-inside and fully-outside fast paths are taken (counters); timing of 1,000,000 instances recorded in a bench line.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** The CPU cull of a million instances matches brute force exactly, is thread-count independent, and its cost is recorded.

## Notes

- Large worlds (Day 494 spec): the cull and the compaction take a render origin (`f64` `Vec3`, zero for now) and write `f32` records relative to it; a test with a set 10,000 km away and the origin at the camera checks records equal a reference computed in `f64`.

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

High-effort review (own pass over all angles): 3 findings, none needing a code change. (1) The chunk tests use a rounding margin so a chunk is rejected or accepted whole only when every instance would be: kept, and the equality with the per-instance test over 200 random cameras (plain, distance and size cuts) is the regression test. (2) `InstanceQuery::Raycast` scans every instance of each crossed chunk (at most 256 each): accepted, bounded. (3) A set culled by size has no whole-chunk fast path because a chunk's smallest scale is not kept: documented in the guide.

Runs: plain `ran 781 of 12088 tests (phases: 3 4 5 6 28; flavors: plain; ...)`, asan `ran 647 of 12094`, tsan `ran 353 of 12100`, all passed; reduced runs. GCC unity-off build clean.

Recorded: bench line (Debug build, 4 cores): 1,000,000 instances, 23,539 visible in 4,197 runs, 5,336 of 5,787 chunks skipped: 4.9 ms on one thread, 1.5 ms on 4. The Release number is for Day 504. Deferred to Day 497: the cull counters in `GBufferStats` and the overlay (the cull has no caller in a frame until the draw path exists); `InstanceQuery` rebuilds its tree whole when the set changes (a dynamic tree is the alternative if a set is edited every frame).
