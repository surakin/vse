# Phase 6 — UI System (Widgets & Viewport)

**Depends on:** Phase 5 (Font & Text — glyph atlas, text layout, grapheme
cursor positions), Phase 4 (Renderer core — 2D batched drawing through
the existing pass/material machinery, render-to-texture), Phase 3 (RHI),
Phase 1 (platform input/window events).
**Goal:** the engine's own retained-mode GUI toolkit — hand-rolled on the
RHI and the Phase 5 text layer, no third-party GUI library (Qt, GTK,
Dear ImGui, etc. are all out under the zero-third-party-convenience-
dependencies rule) — with the commonly used widget set and, in
particular, a **viewport widget** that hosts a rendered 3D world inside
the widget tree. This is the shared UI layer that in-game HUDs/menus,
debug tooling (profiler, console, job-system stats), and the Phase 18
editor all build on, instead of each hand-rolling its own.
**Definition of done:** a resizable application window whose entire
contents are widgets: a menu bar with submenus and a context menu,
buttons, labels, checkboxes, a combobox, a text field, a scrollable list
with working scrollbars, and a viewport widget rendering the Phase 4 lit,
textured cube from a camera inside a resizable panel. Mouse and keyboard
input reach the right widget (a click in the viewport goes to the
viewport, menu popups overlay it), resizing the window re-lays-out the UI
and resizes the viewport's render target, and the UI holds an interactive
frame rate on Linux under both display backends.

## Key deliverables (to be expanded into day-by-day tasks when started)

### Core

- **Retained widget tree.** Retained rather than immediate-mode: menus,
  comboboxes and scroll views carry state across frames (open popups,
  scroll offsets, focus, hover), and popups must overlay arbitrary
  content. A thin immediate-mode-style debug overlay (frame time, job
  stats, log console) may be built first on the same draw layer as an
  early milestone — it is the first thing the job system and renderer
  would use.
- **2D draw layer.** Batched textured/colored quads and rounded rects
  with clip rectangles (needed for scroll views, menus, text fields)
  through the Phase 4 renderer; glyph quads from Phase 5; z-ordered
  layers so popups/tooltips/drag visuals draw above everything.
- **Layout.** Container widgets (horizontal/vertical box, grid, stack/
  overlay, anchors, splitter), measure/arrange pass, DPI/scale factor
  from the platform layer, min/preferred size, re-layout on window
  resize.
- **Input routing.** Hover, focus, keyboard navigation (Tab order),
  mouse capture during drags, wheel routing, popup dismissal on outside
  click, modal dialogs. Needs an **input event stream** from the
  platform layer (press/release, text, wheel, enter/leave) in addition
  to Phase 1's polled `InputState`; see Notes.
- **Theming.** A style/theme struct (colors, metrics, per-state skins,
  fonts) so widgets don't hard-code appearance; light/dark is a stretch.

### Widget set

- Display: **label**, image/icon, progress bar, separator, tooltip.
- Buttons: **button**, toggle button, **checkbox**, radio group.
- Value input: **slider**, spin box, single-line **text field** (cursor
  and selection by grapheme cluster via the Phase 5/foundation string
  support; multi-line text is a stretch), **combobox** (dropdown).
- Lists and scrolling: **scrollbar**, scroll view/container, list box;
  tree view if the editor needs it (may land in Phase 18).
- Menus: **menu bar**, **menus with submenus**, **context menus**,
  keyboard accelerators/mnemonics.
- Containers: panel, tab view, splitter, dialog/window frame, dockable
  panels as a stretch.
- **Viewport widget** (below).

### Viewport widget (hosts the 3D world)

- A widget that owns a **render target** (RHI color + depth texture)
  sized to its laid-out rectangle, plus a **camera binding**; the
  Phase 4 renderer draws the world into that target as a pass scheduled
  before the UI composite, and the UI then draws the resulting texture
  as a quad in the widget's rectangle (clipped like any other widget).
  The UI layer itself does not depend on ECS or scene content — the
  application attaches "what to render" through a callback/binding, so
  the widget is equally usable for a game view, an editor scene view,
  or a material/mesh preview.
- Resizing re-creates (or resizes) the render target when the laid-out
  size changes, **coalesced to once per frame** (the Phase 1 Wayland
  resize bug — a buffer per configure — is the cautionary example), and
  keeps the aspect ratio right for the camera.
- Several viewports at once (multi-view editor, game view + preview),
  each with its own target and camera.
- **Input to the viewport:** pointer coordinates in the viewport's own
  pixel space, enter/leave, focus, mouse capture while dragging,
  wheel; and for camera control, **relative mouse motion with pointer
  lock** (hide/confine the cursor while the user drags or flies). That
  last item requires platform-layer additions (see Notes).
- Overlay content drawn on top of the world inside the viewport
  (HUD text, gizmo handles in the editor) goes through the same 2D draw
  layer with the viewport's clip rect.

## Notes / open questions

- **Platform-layer additions this phase needs (Phase 1 gaps).**
  (a) An *event stream* alongside `InputState`: key press/release,
  text (UTF-8 — `xkb_state_key_get_utf8` on Wayland, `Xutf8LookupString`
  already called but unused on X11), wheel steps, pointer enter/leave,
  key repeat (compositors don't repeat for clients on Wayland; X11
  does), window resize and focus. (b) **Relative pointer + pointer
  lock/confine** for the viewport: Wayland `zwp_relative_pointer_v1`/
  `zwp_pointer_constraints_v1` (new generated protocol bindings, same
  pattern as xdg-shell/xdg-decoration) and XInput2 raw motion or grab/
  warp on X11. (c) **Cursor shape** (resize arrows, text I-beam) via
  `wp_cursor_shape_v1`/`XDefineCursor`. These are Linux platform
  work scheduled as the first days of this phase, behind the existing
  `os::Window` seam so other platforms follow the same shape (PLAN.md
  assumption #11).
- **Clipboard and drag-and-drop** (Wayland data-device protocol, X11
  selections) are deliberately deferred; text fields get copy/paste in a
  later pass. Revisit when the editor needs asset drag-and-drop.
- **IME / complex-script text input** is out of scope (matches Phase 5:
  no shaping). Latin-family text and grapheme-correct cursor movement
  only.
- **Accessibility** (screen readers) is out of scope for now.
- **Retained vs. immediate** is decided above (retained) but the
  debug-overlay milestone is immediate-mode-style on purpose; if that
  split causes duplicated code, fold the overlay into retained widgets.
- **Threading:** UI is built and ticked on the main thread; the job
  system (Phase 2) may be used for off-thread asset/font work, not for
  widget mutation.
- Full day-by-day breakdown deferred until Phase 5 is complete.
