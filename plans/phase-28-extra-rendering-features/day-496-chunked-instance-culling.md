# Phase 28 / Day 496 — Chunked Instance Culling on the CPU

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Day 495.

Cull a million instances cheaply: whole chunks first, instances inside a boundary chunk second.

## Tasks

- [ ] `CullInstances(set, frustum, lod_params, out)`: the chunk bounds go through the frustum test (a fully inside chunk emits all its instances without a per-instance test; a partly inside one tests each; a fully outside one is skipped); the chunk tests are spread over the job scheduler (`jobs`), one job per group of chunks, with the per-job output lists concatenated in chunk order so the visible list is identical for any thread count.
- [ ] A distance cull and an optional maximum draw distance per set; a small-instance cull (projected size below a pixel threshold) as a setting; a visible-instance list holds `(chunk, first, count)` runs, not one entry per instance, when a chunk is fully visible.
- [ ] The spatial module: the set's chunk bounds are inserted into a `spatial` static or dynamic structure so a ray or overlap query finds instances (an `InstanceQuery`: which instance does this ray hit by bounds), and the sync follows `spatial_sync` rules, not a private grid.
- [ ] The renderer's existing frustum type is reused (`renderer::Frustum`); no second frustum code.
- [ ] Counters for the cull (chunks tested, chunks skipped, instances tested, instances emitted) exposed in `RendererInfo` and the overlay, so a user sees the cost.
- [ ] Tests: a brute-force per-instance frustum test equals the chunked result as a set on 100,000 random instances and 200 random cameras; the result is identical at 1 and 8 threads; fully-inside and fully-outside fast paths are taken (counters); timing of 1,000,000 instances recorded in a bench line.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** The CPU cull of a million instances matches brute force exactly, is thread-count independent, and its cost is recorded.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
