# Phase 15 — UI System (Widgets & Viewport)

**Depends on:** Phase 14 (Font & Text — glyph atlas, text layout, grapheme
cursor positions), Phase 5 (Renderer core — 2D batched drawing through
the existing pass/material machinery, render-to-texture), Phase 4 (RHI),
Phase 1 (platform input/window events).
**Goal:** the engine's own retained-mode GUI toolkit — hand-rolled on the
RHI and the Phase 14 text layer, no third-party GUI library (Qt, GTK,
Dear ImGui, etc. are all out under the zero-third-party-convenience-
dependencies rule) — with the commonly used widget set and, in
particular, a **viewport widget** that hosts a rendered 3D world inside
the widget tree. This is the shared UI layer that in-game HUDs/menus,
debug tooling (profiler, console, job-system stats), and the Phase 27
editor all build on, instead of each hand-rolling its own.
**Definition of done:** a resizable application window whose entire
contents are widgets: a menu bar with submenus and a context menu,
buttons, labels, checkboxes, a combobox, a text field, a scrollable list
with working scrollbars, and a viewport widget rendering the Phase 5 lit,
textured cube from a camera inside a resizable panel. Mouse and keyboard
input reach the right widget (a click in the viewport goes to the
viewport, menu popups overlay it), resizing the window re-lays-out the UI
and resizes the viewport's render target, and the UI holds an interactive
frame rate on Linux under both display backends. Theming and render-to-
texture are demonstrated: the same widget tree draws with two different
themes switched at run time, and a UI tree rendered into an offscreen
texture is shown on a quad in a 3D scene (an in-world screen).

**Status: planned (2026-11-10), Days 184-206; not started.** Day numbers continue from Phase 14 (which ends at Day 183). The Windows PC does Day 205; Days 184-204 are done on the Linux machine and leave
their Windows halves open in `KNOWN-ISSUES.md` until then (CLAUDE.md, "Windows is a supported platform").

## Days (Days 184-206; the day files carry the sub-tasks)

1. [Day 184 — Design spec, spikes, the `ui` module skeleton](phase-15-ui-system/day-184-design-spec-and-ui-module.md)
2. [Day 185 — Platform: the input event stream (Wayland, X11)](phase-15-ui-system/day-185-platform-input-event-stream.md)
3. [Day 186 — Platform: relative pointer, pointer lock, cursor shape (Wayland, X11)](phase-15-ui-system/day-186-platform-pointer-lock-and-cursor.md)
4. [Day 187 — Platform: the OS clipboard (Wayland, X11)](phase-15-ui-system/day-187-platform-clipboard.md)
5. [Day 188 — Platform: OS drag and drop (Wayland, X11)](phase-15-ui-system/day-188-platform-drag-and-drop.md)
6. [Day 189 — The canvas and the 2D draw list (CPU)](phase-15-ui-system/day-189-draw-list.md)
7. [Day 190 — The UI renderer: shaders, pipeline, graph pass](phase-15-ui-system/day-190-ui-renderer.md)
8. [Day 191 — Themes](phase-15-ui-system/day-191-theme.md)
9. [Day 192 — Widget tree, surface, offscreen test harness](phase-15-ui-system/day-192-widget-tree-and-surface.md)
10. [Day 193 — Layout and container widgets](phase-15-ui-system/day-193-layout.md)
11. [Day 194 — Input routing, focus, capture, the window adapter](phase-15-ui-system/day-194-input-routing.md)
12. [Day 195 — Display and button widgets](phase-15-ui-system/day-195-display-and-button-widgets.md)
13. [Day 196 — Slider, scrollbar, scroll view, list box](phase-15-ui-system/day-196-value-widgets-and-scrolling.md)
14. [Day 197 — The single-line text field and the spin box](phase-15-ui-system/day-197-text-field.md)
15. [Day 198 — Popups, menus, combobox, tooltips, accelerators](phase-15-ui-system/day-198-popups-and-menus.md)
16. [Day 199 — Tab view, splitter, dialogs, modality](phase-15-ui-system/day-199-containers-and-dialogs.md)
17. [Day 200 — Drag and drop in the toolkit](phase-15-ui-system/day-200-drag-and-drop-in-the-toolkit.md)
18. [Day 201 — The viewport widget](phase-15-ui-system/day-201-viewport-widget.md)
19. [Day 202 — UI surfaces and render-to-texture](phase-15-ui-system/day-202-ui-surfaces-and-render-to-texture.md)
20. [Day 203 — Settings binding and the debug overlay](phase-15-ui-system/day-203-settings-screen-and-debug-overlay.md)
21. [Day 204 — The `ui_demo` application, themes, reset, goldens](phase-15-ui-system/day-204-ui-demo.md)
22. [Day 205 — Windows PC: Win32 events and lock, DXIL, everything built and run](phase-15-ui-system/day-205-windows-pc.md)
23. [Day 206 — Guide, benchmark, closing runs, phase review](phase-15-ui-system/day-206-docs-and-closing-run.md)

## Decisions made when planning (starting positions; Day 184 confirms or changes them with spikes)

- **Module split.** `engine/ui` is CPU-only (tree, layout, theme, input routing, draw list; no RHI type), so it is tested without a GPU and an offscreen surface is the normal case. It sits in layer 11
  beside the RHI (it depends on `text`, the `platform` input headers and the foundation). The GPU side is `renderer::UiRenderer`, `ViewportTarget` and `UiSurfaceTarget` in the renderer, which links `ui`
  for the draw-list type. A thin `ui_window` library (above `ui` and `platform`) is the only place a widget tree meets an `os::Window`.
- **Images** are `ui::ImageId` values the renderer maps to textures by owner (never a cached handle), so the reset rule of CLAUDE.md holds for the viewport and surface textures.
- **Rounded rectangles and borders** are one SDF quad with analytic anti-aliasing (one pipeline); Day 184 spikes it against tessellation.
- **Input** has two layers: the polled `InputState` stays; a bounded event queue (`Window::PollEvent`) is added (Day 185) and is what the UI reads. Wayland needs client-side key repeat.
- **Pointer lock** is `zwp_pointer_constraints_v1` + `zwp_relative_pointer_v1` on Wayland and grab + XInput2 raw motion on X11 (Day 186); cursor shapes use `wp_cursor_shape_v1` / core X cursors, and a window can also take a custom RGBA cursor image (`wl_shm` cursor surface, Xcursor, `CreateIconIndirect`); no software-drawn cursor. If a
  library or protocol is not acceptable under the plan's library line, the fallback is recorded in `KNOWN-ISSUES.md` rather than silently dropped.
- **Clipboard and drag and drop are in this phase** (decided 2026-11-10): the OS clipboard (text, file lists, custom formats, primary selection where it exists) and OS drag and drop in and out, on Wayland (data device) and X11 (selections with INCR, XDND), Win32 on Day 205; always asynchronous with timeouts and with validated, size-capped data. The toolkit side (drag sources, drop targets, list reorder, text drops, OS-backed paste) is Day 200; an in-process clipboard remains for offscreen surfaces and tests. IME, accessibility, docking and file dialogs are non-goals (listed in the Day 206 sweep).
- **Windows.** The Win32 backend of the new platform operations and DXIL for the new shaders are one Windows-PC day (202), so Linux days are not blocked and the "two machines" rule is met at the phase close.
- **Theme rule.** Widgets draw only from the theme; a test fails on a color or pixel-metric literal in widget sources (Day 195).
- **Size.** 23 days, the largest phase so far, because it contains platform work on three backends, a renderer, a toolkit with about twenty widgets, and two integration targets (viewport, in-world surfaces).

## Key deliverables

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
  through the Phase 5 renderer; glyph quads from Phase 14; z-ordered
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
- **Theming (required, not a stretch).** Widgets never hard-code
  appearance: every color, metric (padding, border, corner radius,
  spacing), font and per-state skin (normal/hover/pressed/focused/
  disabled) comes from a **theme** object resolved when a widget draws.
  Requirements to design in from the first widget:
  - A theme is plain data and can be **swapped at run time** (the whole
    tree restyles without being rebuilt); light and dark are just two
    themes, not a special mode.
  - A theme can be set **per subtree and per UI surface** (an in-world
    screen can look different from the editor), with unset values
    falling back to the parent, then to a default theme.
  - Widget **variants** (e.g. primary/danger button, heading label) are
    named style entries in the theme, not subclasses.
  - Themes load from an engine data file (the format belongs to the
    Phase 20 asset pipeline; until then a theme is built in code), and
    scale with the surface's DPI/scale factor.
  - Custom widgets read the same theme, so game UIs and the editor
    share one styling mechanism.

### Widget set

- Display: **label**, image/icon, progress bar, separator, tooltip.
- Buttons: **button**, toggle button, **checkbox**, radio group.
- Value input: **slider**, spin box, single-line **text field** (cursor
  and selection by grapheme cluster via the Phase 14/foundation string
  support; multi-line text is a stretch), **combobox** (dropdown).
- Lists and scrolling: **scrollbar**, scroll view/container, list box;
  tree view if the editor needs it (may land in Phase 27).
- Menus: **menu bar**, **menus with submenus**, **context menus**,
  keyboard accelerators/mnemonics.
- Containers: panel, tab view, splitter, dialog/window frame, dockable
  panels as a stretch.
- **Viewport widget** (below).

### Viewport widget (hosts the 3D world)

- A widget that owns a **render target** (RHI color + depth texture)
  sized to its laid-out rectangle, plus a **camera binding**; the
  Phase 5 renderer draws the world into that target as a pass scheduled
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

### UI surfaces and render-to-texture (in-world UIs)

The viewport widget puts a 3D world *inside* the UI; this is the
opposite direction, and must be possible without redesign: a widget
tree can be rendered **into a texture** that the renderer then uses
like any other (for example on a screen mesh in the world, a hologram,
a monitor, a wrist display).

- **UI surface.** A surface is a widget-tree root plus a target: either
  an OS window's swapchain image or an offscreen RHI color texture of a
  chosen size. Nothing in the widget, layout, theme or draw code may
  assume the target is a window, which means no reaching for
  `os::Window` from inside widgets: size, DPI/scale, focus and time are
  properties of the surface.
- **Offscreen rendering.** Each offscreen surface renders through the
  same 2D draw layer and render graph as the window UI, as a pass
  scheduled before the passes that sample its texture. Update policy per
  surface: every frame, on demand (only when the tree is dirty), or
  at a fixed rate, so a dozen idle screens cost nothing. The texture is
  exposed to the renderer/material system as an ordinary sampled
  texture; no UI types leak into the renderer.
- **Input without a window.** The UI consumes an abstract input event
  stream (pointer position and buttons, wheel, keys, text) fed to a
  surface by whoever owns it. A window feeds its surface from the
  platform event stream; the game feeds an in-world surface from a ray
  cast against the screen mesh (hit point mapped to surface pixels),
  or leaves it display-only. Mapping a ray hit to UI input is the
  application's job (it needs the scene), the surface just accepts
  events, including pointer capture and focus per surface.
- **Per-surface settings:** its own size and scale factor, theme,
  clear/background (opaque or transparent for overlays), and sRGB
  handling, so the same tree looks right in a window and on a lit
  surface (an emissive material is the expected way to show it).
- **Resize and lifetime:** surface size changes are coalesced to once
  per frame like the viewport's; offscreen textures are created and
  destroyed through the device's deferred destruction.
- Out of scope here: curved or tessellated screens beyond what a normal
  UV-mapped mesh gives, VR/world-space interaction rays, and mipmapping
  or filtering policy beyond what the renderer already offers for any
  texture. Placing a surface on an entity needs Phase 17 (ECS); this
  phase demonstrates it on a hand-placed quad.

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
  selections and XDND, Win32 OLE) were deferred in the original sketch and
  are in this phase since 2026-11-10: platform Days 187-188, toolkit Day
  200, Win32 Day 205.
- **IME / complex-script text input** is out of scope (matches Phase 14:
  no shaping). Latin-family text and grapheme-correct cursor movement
  only.
- **Accessibility** (screen readers) is out of scope for now.
- **Retained vs. immediate** is decided above (retained) but the
  debug-overlay milestone is immediate-mode-style on purpose; if that
  split causes duplicated code, fold the overlay into retained widgets.
- **Design constraint from the surface requirement:** because a window
  is just one kind of target, decide early (Day-level design in this
  phase) how a surface receives size/scale/time/input, and test the
  widget code against an offscreen surface from the first widget, not
  as a late add-on. Same for themes: the first widget is written against
  the theme API.
- **Threading:** UI is built and ticked on the main thread; the job
  system (Phase 2) may be used for off-thread asset/font work, not for
  widget mutation.
- The day-by-day breakdown is below (written 2026-11-10, after Phase 14 closed on Linux).

## What Phase 5 gives this phase (Phase 5 Day 84)

- **Pass ordering.** UI is an overlay pass after the tonemap (see Phase 14's notes): the graph orders passes by the resources they read
  and write, so the UI pass declares the tonemap target as its write and runs last; in-game UI, debug tooling and the editor share it.
- **The viewport widget** renders a 3D world with `Renderer::RenderFrame` into an offscreen `FrameTarget` (a texture the widget then
  samples as an image), using the same frame callback as a window. Resizing the widget is a new target size: the graph and the transient
  pool follow it with no other change (the swapchain resize test in `tests/renderer/renderer_test.cpp` covers that path for the swapchain).
- **UI surfaces and render-to-texture** use the same offscreen target path; a UI surface in the world is a material whose texture is
  that target.
- The overlay pass is forward (blended), so it needs a pipeline target without the G-buffer attachments: `MaterialSystem::GetPipeline`
  takes the `PipelineTarget`, so UI materials just ask for it.

- **Settings and files (from Phases 9 and 11, planned 2026-11-06):** settings screens bind to the Phase 11 settings registry (labels, ranges, groups and restart-required flags come from its metadata); file dialogs and recent-file lists use `io::Path`.

- **Settings binding (from Phase 11 Day 139):** a settings screen is generated from `settings::Registry` metadata (`label`, `description`, `group`, ranges and `RangePolicy`, enum names, `kRestartRequired`/`kAdvanced`/`kHidden`/`kSecret` flags), writes through `Settings::Apply` (so a rejected change rolls back and the widget shows the reason) and refreshes from `Settings` listeners and `OnChange` rather than polling; provenance (`Settings::Provenance`) lets a widget show that a value is pinned by the environment or the command line and cannot be edited.
