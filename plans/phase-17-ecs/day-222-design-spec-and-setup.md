# Phase 17 / Day 222 — Design Spec, Decisions & Phase Setup

**Phase:** [Phase 17 — ECS](../phase-17-ecs.md)
**Depends on:** Phase 16 complete; Phase 2 (jobs), Phase 5 (renderer), Phase 10 (containers).

Settle the ECS design, write the spec, define the benchmark baseline and set up the phase before any storage code.

## Tasks

- [ ] **`CURRENT_PHASE` becomes 17** in the commit that completes this day's first task, with the `tests/phases.txt` line for phase 17 (`ecs`, `scene`) and `flavors 17`.
- [ ] **Spec** `docs/superpowers/specs/2026-11-NN-ecs-design.md` recording the decisions below with the reason for each (the planner's defaults in brackets; an experiment that disproves one amends the later day files in the same commit):
      - storage [**archetypes with fixed-size chunks**, 16 KiB, structure-of-arrays columns inside a chunk; chunk capacity from the archetype's row size; entities of one archetype fill chunks in creation order; removal is swap-with-last inside the archetype, with the moved entity's table entry fixed up];
      - entity ids [64-bit: 32-bit index + 32-bit generation, the all-zero value invalid, like the RHI handles and `renderer::ObjectId`; the entity table holds (archetype, chunk, row, generation) per slot];
      - component types [registered at first use, an id from a process-wide counter (no RTTI, exceptions stay off); each type has a `TypeInfo` with size, alignment, move-construct, destroy and a trivially-relocatable flag; zero-size types are tags; components may be non-trivial (a `SharedPtr<GpuMesh>`), the per-type function pointers keep the trivial case a `MemCopy`; a hard cap of 128 component types per world and 32 per archetype, with a counted refusal, documented];
      - structural changes [direct changes are legal outside iteration; during iteration they go through a `CommandBuffer` applied at a sync point; an assert (`Aborts:`) catches a direct change while a query is iterating];
      - queries [typed terms `Read<T>`, `Write<T>`, `Optional<T>`, `With<T>`, `Without<T>`, `Changed<T>`; matching archetypes cached per query and updated incrementally from an archetype version counter; two iteration forms: per entity (`ForEach(fn(Entity, A&, const B&))`) and per chunk (`ForEachChunk` giving spans, the form that vectorizes and parallelizes)];
      - change detection [a world tick counter and a per-column, per-chunk last-written tick; `Write<T>` marks the chunk's column; `Changed<T>` skips unmodified chunks];
      - systems [a system is a function plus a declared access set (read/write component types, resources); stages give coarse ordering; within a stage, systems whose access sets do not conflict run in parallel as jobs joined by a `Counter` (Phase 2, `PLAN.md` assumption #15), conflicting ones run in registration order; the result of a frame is deterministic: the same as running every system in registration order];
      - resources [singletons per world, one per type, access-declared like components];
      - transforms and hierarchy [`LocalTransform`, `WorldTransform`, `Parent`; a propagation system computes world matrices parent-first, parallel per hierarchy depth];
      - the renderer seam [`renderer::RenderScene` stays the renderer's input, unchanged (`GBufferPass::Prepare` and `FrameResources::WriteLights` take it by const reference); a new module `engine/scene` (above `ecs` and `renderer`) holds the components `MeshRenderer`, `Light`, `Camera` and a sync system that mirrors entities into the `RenderScene` incrementally with `Changed<T>`; `engine/ecs` itself includes no renderer header];
      - settings [`ecs.parallel_systems` (bool, default true: false runs everything sequentially, the debugging switch), `ecs.stats` (per-system timing); chunk size is a compile-time constant];
      - not in this phase [serialization and prefabs from files (Phase 21), events and observers (a note in the spec: added when the first consumer needs them), entity relationships beyond `Parent`, a network replication story, sparse-set storage for rare components (benchmarked on Day 235, added only if the numbers ask for it)].
- [ ] **Module rows:** `engine/ecs` (depends on foundation and jobs; layer 10 beside `jobs`/`text`, allowed edge `ecs -> jobs`) and `engine/scene` (depends on `ecs` and `renderer`; layer 13) in `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, the CMake libraries (each with `_asan` and `_tsan` flavors), `docs/reference/modules.txt`; `engine/ecs` includes no renderer, RHI or platform header (the gate enforces it).
- [ ] **Benchmark definition:** the naive OOP baseline is fixed in the spec so Day 235 cannot move the goalposts: `Vector<UniquePtr<GameObject>>` with a virtual `Update(dt)`, objects holding their components as members and `Vector<UniquePtr<Component>>` for the optional ones, heap-allocated individually in creation order and then shuffled (the realistic allocation pattern); the same work in both implementations (integrate position by velocity, a damped spring on a second component, a transform-to-matrix update, a conditional on a tag) at 1,000, 10,000, 100,000 and 1,000,000 entities; metrics: time per frame (median of 200 frames), cache misses where `perf` is available, bytes per entity, creation and destruction throughput; the pass condition for the phase: the ECS iteration is at least 3x faster than the baseline at 100,000 entities and parallel scaling reaches 70 percent efficiency on 4 workers for the heavy system (numbers confirmed or amended on Day 235 with the measured hardware recorded).
- [ ] **Experiments:** (a) 16 KiB versus 64 KiB chunks on a toy SoA loop (per-entity time, iteration over 1M entities), to fix the chunk size; (b) entity-id layout: lookup cost of 64-bit generation ids versus plain indices; (c) the cost of per-type function-pointer relocation versus `MemCopy` for trivial types, to decide whether the world special-cases trivial columns.
- [ ] **Plan check:** read Days 223-236 against the spec; amend any day the decisions changed; `PLAN.md` gets an assumption entry for the ECS design (archetype storage, scheduler contract); `plans/phase-18-3d-text-and-color-glyphs/day-245-*.md` (3D text entities) is checked against the `engine/scene` design and amended if it changed.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime or memory code (always for storage days); quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the spec records every decision with its evidence and fixes the benchmark baseline, `CURRENT_PHASE` is 17, the module rows exist, and Days 223-236 are still accurate (or amended in this commit).

## Notes

- Planned 2026-11-10. The defaults in brackets are the planner's; the user is asked about none of them (design choices inside the plan), but the pass condition numbers are the user's to override.

## Post-implementation code review

(Filled in at the end of the day.)
