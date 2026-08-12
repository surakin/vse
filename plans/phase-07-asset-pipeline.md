# Phase 7 — Asset Pipeline

**Depends on:** Phase 4 (ECS), Phase 3 (Renderer core).
**Goal:** custom binary asset formats plus offline tooling to produce
them, and a runtime loader — the foundation Phase 8 (Unreal import)
targets when converting external content into engine-native form.
**Definition of done:** an offline cooker converts source assets (in our
own custom formats) into engine-ready binary blobs, loaded at runtime by
the engine with no editor/tool code linked into the game binary.

## Key deliverables (to be expanded into day-by-day tasks when started)

- Custom mesh format (vertex/index buffers, LODs, material slot
  references) — this is also the target format Phase 8's Unreal mesh
  importer writes into.
- Custom texture format (wraps GPU-native compressed payloads — BCn/ASTC —
  per assumption #4 in root `PLAN.md`).
- Custom material format (shader reference + parameters).
- Custom level/scene format (entity/component data, referencing the above).
- Offline cooker tool(s) under `/tools` that produce these formats.
- Runtime loader with (at least) synchronous loading; async/streaming is a
  later refinement.

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 4 is complete.
- Asset versioning/migration strategy should be decided here, since Phase
  8's importer will be a second producer of these same formats and both
  need to stay in sync.
