# The UI toolkit

Status: draft (Phase 15, Day 184). The module exists and builds; the toolkit's first types arrive on Day 189. Design and reasons: `docs/superpowers/specs/2026-11-10-ui-design.md`.

## The model

- The toolkit lives in `engine/ui` (namespace `engine_placeholder::ui`). It is **CPU only**: a retained tree of widgets, layout, themes, input routing and a 2D draw list. It names no
  RHI type; the renderer (`renderer::UiRenderer`) turns the draw list into one render-graph pass.
- A **surface** owns a widget tree, a size in **logical units**, a **scale** factor, a clock, a theme and the focus. A window is only one kind of thing that feeds a surface; an offscreen
  texture (an in-world screen) is another, and tests use one without any window.
- Everything is main-thread only.

More sections arrive with the days that build them (draw list, renderer, theme, tree, layout, input, widgets, popups, drag and drop, viewport, surfaces).
