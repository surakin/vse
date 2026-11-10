# Phase 18 / Day 248 — Stats, Debug Draw, Settings, Benchmarks & the Demo

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Days 237-247.

Make the system observable and measure it against the baselines fixed on Day 237.

## Tasks

- [ ] **Stats and settings:** `SpatialStats` per set (nodes, depth, quality metric, moves, re-inserts, queries, visited nodes per query, build and sync time); the four settings of the spec registered in `settings::Registry` with their env and flag names; a `--spatial-stats` line in the demos.
- [ ] **Debug draw:** `spatial::DebugLines(set, options, out)` yields line segments (node boxes by depth, leaf boxes, last query volume and hits) for the renderer's line pass to draw (the drawing is the demo's; the module holds no renderer type); `spatial.debug_draw` toggles it in the demo.
- [ ] **Benchmarks** (`tests/spatial/spatial_bench.cpp`, tag `[benchmark]`, not in the default run): the workloads of Day 237 at 1k, 10k, 100k and 1M items: build, update (1 percent and 100 percent moving), overlap, ray, frustum, pair update; the BVH culling against the flat scan; parallel scaling of the sync and of batched queries on 1, 2, 4 workers; results written into the spec and the day file with the hardware, compared with the seven-point pass condition in section 4 of `docs/superpowers/specs/2026-11-10-spatial-design.md` (culling gain 8x at 100,000 and 30x at 1,000,000; a quiet scene at most 5 percent of a flat scan; 1.0 us per moved object at 1 percent; 80 ms for 100,000 moving agents with the pair feed; query and memory bounds; exactness); a miss is profiled, fixed, re-run and, if still missed, filed and presented to the user.
- [ ] **Demo `game/spatial_demo`:** 50,000 entities (static city blocks and moving agents) with the render set culling, a physics-set pair counter, a mouse ray that picks the entity under the cursor, a radius query that highlights neighbors, the debug draw, `--backend`, `--frames`, `--screenshot` and the settings flags like the other demos; a golden image for the null/Vulkan path; a README checked by `spatial_demo_readme_current`.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (tsan: always, the parallel benchmark paths); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the demo README and `docs/guide/spatial.md`: "Measuring and debugging".
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the stats, debug lines and settings exist, the benchmark meets (or honestly reports missing) the pass condition, and a demo shows culling, picking and neighbor queries on 50,000 entities.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
