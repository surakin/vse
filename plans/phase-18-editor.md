# Phase 18 — Editor Tooling

**Depends on:** Phase 6 (UI system), Phase 7 (ECS), Phase 10 (Asset pipeline), Phase 11
(Scripting: Lua/LuaJIT) — the editor is built using the engine's own
renderer/ECS and Phase 6's UI system, not a separate GUI toolkit.
**Goal:** an in-engine editor for building content without hand-writing
asset files.
**Definition of done:** a scene view, entity inspector, and asset browser
exist and can build/modify a level that the runtime can load.

## Key deliverables (to be expanded into day-by-day tasks when started)

- Editor-specific widgets on top of **Phase 6's UI system** (which owns
  the widget set, layout, input routing and the viewport widget): tree
  view/outliner, property grid, dockable panels, asset thumbnails.
  Nothing here reimplements general widgets or text.
- Scene view built on Phase 6's **viewport widget**, with gizmos
  (translate/rotate/scale) drawn as viewport overlays.
- Entity inspector reflecting ECS component data.
- Asset browser over the Phase 10 asset pipeline's minted output.
- Save/load round-trip through the Phase 10 level format.

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 11 is complete.
- **UI and text (resolved).** Phase 5 owns glyph rasterization/layout and
  Phase 6 owns the widget toolkit and viewport widget; this phase
  consumes both. Whether the editor needs any *additional* general
  widgets beyond Phase 6's set is decided against what Phase 6 delivers.
