# Phase 25 — Editor Tooling

**Depends on:** Phase 15 (UI system), Phase 16 (ECS), Phase 19 (Asset pipeline), Phase 20
(Scripting: Lua/LuaJIT) — the editor is built using the engine's own
renderer/ECS and Phase 15's UI system, not a separate GUI toolkit.
**Goal:** an in-engine editor for building content without hand-writing
asset files.
**Definition of done:** a scene view, entity inspector, and asset browser
exist and can build/modify a level that the runtime can load.

## Key deliverables (to be expanded into day-by-day tasks when started)

- Editor-specific widgets on top of **Phase 15's UI system** (which owns
  the widget set, layout, input routing and the viewport widget): tree
  view/outliner, property grid, dockable panels, asset thumbnails.
  Nothing here reimplements general widgets or text.
- Scene view built on Phase 15's **viewport widget**, with gizmos
  (translate/rotate/scale) drawn as viewport overlays.
- Entity inspector reflecting ECS component data.
- Asset browser over the Phase 19 asset pipeline's minted output.
- Save/load round-trip through the Phase 19 level format.

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 20 is complete.
- **UI and text (resolved).** Phase 14 owns glyph rasterization/layout and
  Phase 15 owns the widget toolkit and viewport widget; this phase
  consumes both. Whether the editor needs any *additional* general
  widgets beyond Phase 15's set is decided against what Phase 15 delivers.
