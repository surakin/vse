# Phase 15 — Editor Tooling

**Depends on:** Phase 4 (ECS), Phase 7 (Asset pipeline), Phase 9
(Scripting VM) — the editor is built using the engine's own renderer/ECS,
not a separate GUI toolkit.
**Goal:** an in-engine editor for building content without hand-writing
asset files.
**Definition of done:** a scene view, entity inspector, and asset browser
exist and can build/modify a level that the runtime can load.

## Key deliverables (to be expanded into day-by-day tasks when started)

- Immediate-mode or retained-mode in-engine UI system (built on the Phase
  2 RHI — this is itself a small sub-project: text rendering, layout,
  input routing for editor widgets).
- Scene/viewport view with gizmos (translate/rotate/scale).
- Entity inspector reflecting ECS component data.
- Asset browser over the Phase 7 asset pipeline's cooked output.
- Save/load round-trip through the Phase 7 level format.

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 9 is complete.
- Whether the editor needs its own UI framework or can reuse a minimal
  subset built for in-game HUD/UI needs (if any exist by this point) —
  decide against what actually exists at the time.
