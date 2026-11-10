# Phase 18 / Day 237 — Design Spec, Experiments & Phase Setup

**Phase:** [Phase 18 — Spatial Partitioning](../phase-18-spatial-partitioning.md)
**Depends on:** Phase 17 complete (Days 229, 232 and 233 in particular); Phase 2 (jobs), Phase 5 (`renderer::Aabb`, `Frustum`, `RenderScene::Cull`).

Settle the spatial design, write the spec, fix the benchmark baselines and set up the phase before any structure is written.

## Tasks

- [ ] **`CURRENT_PHASE` becomes 18** in the commit that completes this day's first task, with phase 18 named in `tests/phases.txt` (its `flavors 18` line and the `tests/spatial/` mapping arrive with the first test, Day 238).
- [ ] **Spec** `docs/superpowers/specs/2026-11-NN-spatial-design.md` recording the decisions below with the reason for each (the planner's defaults in brackets; an experiment that disproves one amends the later day files in the same commit):
      - one module [`engine/spatial`, layer 10 beside `ecs`, depending on `foundation`, the geometry types of `math`, `ecs` and `jobs`; the physics, renderer, scene, audio and gameplay code depend on it, it depends on none of them];
      - two tiers [the **structures** (`StaticBvh`, `DynamicBvh`, `HashGrid`) are generic over a `u64` key and know nothing of the ECS, so they are unit-testable and reusable (a particle system, a tool); the **ECS tier** (components, the `SpatialWorld` resource, the sync system, entity queries) is what systems use];
      - structures [a **static BVH** built top-down with binned surface-area heuristic for geometry that does not move; a **dynamic BVH** (incremental AABB tree with fat margins, insertion by minimum cost, rotations, periodic partial rebuild) for what moves; a **uniform hash grid** only if the experiment says it wins for many same-size dynamic objects (crowds, particles); no octree unless the experiment says so];
      - sets [a world has several named **spatial sets** (`render`, `physics`, `audio`, `gameplay`; up to 8, a bitmask on the entity says which an entity joins), each with its own static and dynamic structures and its own layer mask, so the renderer's culling tree does not hold invisible physics triggers and vice versa];
      - queries [overlap with a box, sphere or frustum; ray cast (closest, any, all); sweep of a box; nearest-k; all overlapping pairs (the broad phase); every query takes a layer mask and an optional filter; results come out in a documented deterministic order];
      - the physics contract [a `PairTracker` keeps the overlap pairs between frames and reports begin and end, deterministically; fat margins are internal; the narrow phase is Phase 20's];
      - the renderer contract [a parallel `CullFrustum` that yields the visible slots; `RenderScene` and its callers stay unchanged apart from taking a precomputed visible list];
      - bounds [`SpatialBounds` (local-space box, from the mesh), `WorldBounds` (computed from `WorldTransform` only when it changed), a `Static` tag that never moves an entity out of the static tree, a `SpatialLayer` component];
      - determinism and threads [the same inputs build the same tree (a parallel build equals the sequential one byte for byte); queries are read-only and thread-safe between updates; updates run as a system after transform propagation];
      - settings [`spatial.parallel` (bool, default true), `spatial.fat_margin` (float, world units, default 0.1), `spatial.stats`, `spatial.debug_draw`];
      - not in this phase [a narrow phase or any collision shape beyond the box and sphere used as bounds (Phase 20), navigation meshes, a 2D variant, GPU-side culling, streaming of world cells (Phase 31), serialization of trees (rebuilt on load)].
- [ ] **Module rows:** `engine/spatial` listed as a planned module in `ARCHITECTURE.md` (section 5, "Planned modules (Phase 18, spatial)"); the row in section 1, `tools/module_deps_check/modules.txt`, the CMake libraries with `_asan`/`_tsan` flavors and `docs/reference/modules.txt` arrive on Day 238, with the first code.
- [ ] **Experiments** (throwaway programs, recorded in the spec): (a) static BVH, dynamic BVH, uniform grid and a loose octree on three workloads (a 100k-box static city, 100k boxes moving 1 percent per frame, 100k same-size agents moving every frame): build time, update time per moved object, overlap-query time, ray-cast time, bytes per object; (b) fat-margin sizes 0, 5, 10 and 25 percent of the object size on the moving workloads (re-insert rate against query cost); (c) the flat scan the renderer does today (`RenderScene::Cull`, 0.08 us per object) against a BVH frustum query at 1k, 10k, 100k and 1M objects, to find the crossover and set the benchmark's pass condition.
- [ ] **Benchmark definition** fixed in the spec so Day 248 cannot move the goalposts: the workloads and sizes of experiment (a), the metrics, and the pass condition for the phase (the numbers are set from experiment (c): the BVH culling must beat the flat scan by at least the factor the spec states at 100,000 objects, and a mostly-static scene must cost under 5 percent of a flat scan per frame when nothing moved).
- [ ] **Plan check:** read Days 238-249 against the spec and amend any day the decisions changed; `PLAN.md` gets an assumption entry for the spatial design; `plans/phase-20-physics.md` (broad phase) and `plans/phase-17-ecs/day-233-scene-bridge.md` (`RenderScene` culling) are checked against it and amended if the contracts differ.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (always: this day changes only plan and spec files, so a reduced run is enough); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the spatial module is portable code; it is built and tested on the Windows PC no later than Day 249, and until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/spatial.md` is started on Day 238.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the spec records every decision with its evidence and fixes the benchmark baseline, `CURRENT_PHASE` is 18, the module row is planned, and Days 238-249 are still accurate (or amended in this commit).

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
