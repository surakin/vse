# Phase 4 — ECS

**Depends on:** Phase 3 (Renderer core).
**Goal:** the data-oriented entity/component/system core that everything
gameplay-facing builds on, replacing Phase 3's hard-coded scene list.
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
- Integration with the Phase 3 renderer: a `Transform` + `Mesh` component
  pair drives what gets drawn, replacing the hard-coded scene list.

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 3 is complete.
- Multithreaded system scheduling depends on Phase 1's threading
  primitives; confirm they're sufficient or need extending.
