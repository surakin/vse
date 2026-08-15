# Phase 17 — Editor Tooling

**Depends on:** Phase 6 (ECS), Phase 9 (Asset pipeline), Phase 10
(Scripting: Lua/LuaJIT) — the editor is built using the engine's own
renderer/ECS, not a separate GUI toolkit.
**Goal:** an in-engine editor for building content without hand-writing
asset files.
**Definition of done:** a scene view, entity inspector, and asset browser
exist and can build/modify a level that the runtime can load.

## Key deliverables (to be expanded into day-by-day tasks when started)

- Immediate-mode or retained-mode in-engine UI system (built on the
  Phase 3 RHI, and on Phase 5's text/glyph layer for widget labels and
  text input rather than reimplementing font rendering here — layout
  and input routing for editor widgets are still this phase's own
  work).
- Scene/viewport view with gizmos (translate/rotate/scale).
- Entity inspector reflecting ECS component data.
- Asset browser over the Phase 9 asset pipeline's cooked output.
- Save/load round-trip through the Phase 9 level format.

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 10 is complete.
- **Text rendering (resolved).** Phase 5 (Font & Text Rendering, added
  after this phase's original note about text being "a small
  sub-project" of its own) now owns FreeType-backed glyph rasterization
  and basic layout — the editor's UI system reuses that rather than
  building its own. Whether the editor needs anything *beyond* Phase
  5's layer (widget layout containers, styling/theming) is still open,
  decide against what Phase 5 actually delivers.
