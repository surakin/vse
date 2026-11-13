# Phase 22 / Day 337 — Benchmarks, Guides & the Spec's Results

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Days 313-328.

Measure against the pass conditions, finish the documentation, and put the numbers in the spec.

## Tasks

- [ ] **Animation figures (added 2026-11-13):** run `[.][animation-bench]` and `[.][render-bench]` (Days 321, 322), record clip size per second against the Day 320 figure, samples per second, 10,000 characters through the system at 1 and 8 workers and the skinned draw cost in the spec, and ask the user to confirm the planner's default pass conditions (4 ms for 10,000 40-joint characters on 8 workers; a minted clip within the declared error bounds; no skinned instance drawn with a stale pose) with the figures beside them; a miss goes to `KNOWN-ISSUES.md`.
- [ ] **Guide pages:** `docs/guide/animation.md` (the skeleton and clip formats, the runtime, blending, the palette, skinning budget, retargeting and morph targets as filed limits) with compiled samples.
- [ ] **Benchmarks** (`tests/asset/asset_bench.cpp`, hidden `[.][asset-bench]`, Release): pak open and lookup, ranged read throughput (pak, loose, network over loopback), blob validation speed, texture mint time and BC1/BC4/BC5/BC7 encode speed per megapixel at 1 and 8 workers, mesh load, a 10,000-resource level's load time (synchronous and asynchronous), the frame cost of `Update` with the budget; recorded in a table in the spec against the pass conditions of Day 313's spec.
- [ ] **Load performance pass conditions** (loading is paramount, the user, 2026-11-13; the numbers are set by Day 313's spike 5 and are the planner's defaults here): minted mesh and texture blobs load at at least 90 % of the raw sequential read rate of the same bytes (CPU time of the load path at most 10 % of the read time, measured with a warm and a cold cache), `load_conversions` is zero and bytes copied beyond the read and the upload are zero, a 10,000-entity level's bulk component load costs under 1 ms per 1,000 entities, an asynchronous load adds no more than one frame of latency beyond the read, and per-load allocations are bounded (one aligned buffer per blob); a miss is a defect to fix before the phase closes or an item for the user, not an accepted default.
- [ ] **Pass conditions** (set in the spec on Day 313, planner's defaults to be confirmed by the user when this day is reached): a pak lookup under 1 microsecond at 100,000 entries; sequential pak read at least 80 % of the loose-file read rate; BC1 encode at least 20 megapixels per second on 8 workers; BC7 at `normal` at least 2 megapixels per second on 8 workers; a 10,000-resource level fully resident within a time the spec states with a frame-time impact (p99) under 1 ms while streaming; misses are listed in `KNOWN-ISSUES.md` with options, not silently accepted.
- [ ] **Guides:** `docs/guide/assets.md` finished (ids and paths, the blob container, versions and upgrades, backends and mounts, the pak format, each type's format and source form with examples, texture compression and the quality settings, the manager and handles, async loading and the budget, the network protocol and cache, `asset_mint` and its project file, determinism, settings, how to add a resource type or a backend, security notes); the format reference tables for the four types; `docs/tutorials/loading-a-level.md` linked from the tutorial index.
- [ ] **Spec** updated to what was built with the measured tables and every deviation from Day 313.
- [ ] **Hostile-input pass:** a fuzz of every blob reader (mesh, texture, material, level), the pak reader and the protocol parser with random and mutated inputs (counts bounded, nothing allocated from an unchecked length), recorded like Day 279's.
- [ ] **Known issues sweep:** `KNOWN-ISSUES.md` gets every limit filed this phase (no compression in paks, no ASTC, BC7 subset, no network write path, no encryption, glTF extensions beyond the supported list, no MikkTSpace tangents, no `KHR_mesh_quantization`).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 338; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the benchmark table is in the spec against its pass conditions, the guide and the tutorial are finished and `ctest -L docs` passes.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
