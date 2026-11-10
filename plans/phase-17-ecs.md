# Phase 17 — ECS

**Depends on:** Phase 2 (job system), Phase 5 (Renderer core), Phase 10 (containers), Phase 11 (settings), Phase 16 (wrappers: all new code uses them).
**Goal:** the data-oriented entity/component/system core that everything
gameplay-facing builds on, replacing Phase 5's hard-coded scene list.
**Definition of done:** thousands of entities updated per frame via
systems, benchmarked against a naive OOP baseline to confirm the
data-oriented layout actually wins.

## Days

222. [Design Spec, Decisions & Phase Setup](phase-17-ecs/day-222-design-spec-and-setup.md)
223. [Entity Ids & the Entity Table](phase-17-ecs/day-223-entities.md)
224. [Component Types & the Registry](phase-17-ecs/day-224-component-registry.md)
225. [Archetypes & Chunk Storage](phase-17-ecs/day-225-archetypes-and-chunks.md)
226. [The World: Create, Destroy, Add, Remove & Migration](phase-17-ecs/day-226-world-and-migration.md)
227. [Queries & Iteration](phase-17-ecs/day-227-queries.md)
228. [Command Buffers & Resources](phase-17-ecs/day-228-commands-and-resources.md)
229. [Change Detection](phase-17-ecs/day-229-change-detection.md)
230. [Systems & the Sequential Scheduler](phase-17-ecs/day-230-systems-sequential.md)
231. [Parallel Scheduling Through the Job System](phase-17-ecs/day-231-parallel-scheduling.md)
232. [Transforms & Hierarchy](phase-17-ecs/day-232-transforms-and-hierarchy.md)
233. [The Scene Bridge: Entities to the Renderer](phase-17-ecs/day-233-scene-bridge.md)
234. [Port the Demos & Remove the Hard-Coded Scene List](phase-17-ecs/day-234-port-demos.md)
235. [Benchmarks Against the Naive OOP Baseline](phase-17-ecs/day-235-benchmarks.md)
236. [Docs, Windows PC, Closing Full Run & Phase Review](phase-17-ecs/day-236-docs-closing-run-and-review.md)

Order reasoning: storage before API (entities, components, archetypes), the structural API before queries, queries before commands (commands exist because queries forbid structural change), change detection before systems because the scheduler's statistics and the render sync use it, sequential scheduling before parallel so the parallel result has a reference to equal, transforms before the scene bridge because the bridge reads `WorldTransform`, the demo port after the bridge, and the benchmark last so it measures the finished design. The 3D text phase (18) follows and builds on `engine/scene`.

**Status: built and closed on Linux (2026-11-10, Days 222-236); the Windows PC run (Day 236) is outstanding and listed in `KNOWN-ISSUES.md`, so the phase is complete only when it is done.** Moved ahead of 3D text on 2026-11-10 (the user's decision); the phases were renumbered (ECS 17, 3D text 18, everything after unchanged).

## Key deliverables (the original list; the days above break it down)

- Archetype/chunk-based component storage.
- Entity creation/destruction, component add/remove, archetype migration.
- Query interface for systems (iterate all entities with a given component
  set) with cache-friendly iteration.
- System scheduling (at minimum sequential; parallel scheduling across
  independent component sets is a stretch goal for this phase, not a
  blocker).
- Integration with the Phase 5 renderer: a `Transform` + `Mesh` component
  pair drives what gets drawn, replacing the hard-coded scene list.

## Notes / open questions

- Day files written 2026-11-10 (Days 222-236). Parallel system scheduling is a deliverable here (Days 231), not a stretch goal: the engine's rule is that every multithreaded subsystem goes through the job system.
- Parallel system scheduling (the stretch goal above) schedules work
  through Phase 2's job system directly — systems over independent
  component sets become jobs joined via a `Counter`, rather than this
  phase managing its own thread pool. This resolves what was previously
  an open question here (whether Phase 1's raw threading primitives
  alone would be sufficient) — they aren't meant to be used directly by
  engine-level code at all; Phase 2 is the intended seam.

## What Phase 5 gives this phase (Phase 5 Day 84)

- **The scene list is replaced, not wrapped.** `renderer::RenderScene` is a fixed-capacity list of objects (mesh pointer, material index,
  transform) and lights, with generation-checked ids; the renderer reads it through `ObjectAt(slot)`/`SlotAlive`. With an ECS, a
  system that owns the transform, mesh and material components fills a `RenderScene` each frame (or the scene is replaced by a
  component query that yields the same `RenderObject` records), keeping the renderer unchanged: `GBufferPass::Prepare` and
  `FrameResources::WriteLights` take the scene by const reference only.
- Culling and sorting run per frame over the scene list on one thread (0.08 us per object in `renderer_bench`); an ECS can move culling
  into a parallel system that produces the visible list, which is the first thing the Phase 5 profile suggests. (Phase 18 does this through the
  render spatial set: a frustum query over a BVH instead of a flat scan, with the flat scan kept as the reference.)
- `GpuMesh` is held by pointer and must outlive the objects that use it; the ECS mesh component owns or shares the `GpuMesh`.
