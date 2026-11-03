# Phase 7 — ECS

**Depends on:** Phase 4 (Renderer core).
**Goal:** the data-oriented entity/component/system core that everything
gameplay-facing builds on, replacing Phase 4's hard-coded scene list.
**Definition of done:** thousands of entities updated per frame via
systems, benchmarked against a naive OOP baseline to confirm the
data-oriented layout actually wins.

## Key deliverables (to be expanded into day-by-day tasks when started)

- Archetype/chunk-based component storage.
- Entity creation/destruction, component add/remove, archetype migration.
- Query interface for systems (iterate all entities with a given component
  set) with cache-friendly iteration.
- System scheduling (at minimum sequential; parallel scheduling across
  independent component sets is a stretch goal for this phase, not a
  blocker).
- Integration with the Phase 4 renderer: a `Transform` + `Mesh` component
  pair drives what gets drawn, replacing the hard-coded scene list.

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 4 is complete.
- Parallel system scheduling (the stretch goal above) schedules work
  through Phase 2's job system directly — systems over independent
  component sets become jobs joined via a `Counter`, rather than this
  phase managing its own thread pool. This resolves what was previously
  an open question here (whether Phase 1's raw threading primitives
  alone would be sufficient) — they aren't meant to be used directly by
  engine-level code at all; Phase 2 is the intended seam.
