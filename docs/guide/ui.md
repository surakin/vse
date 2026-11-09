# The UI toolkit

Status: draft (Phase 15, Days 184 and 189). The module and its canvas and draw list exist; widgets arrive from Day 192. Design and reasons: `docs/superpowers/specs/2026-11-10-ui-design.md`.

## The model

- The toolkit lives in `engine/ui` (namespace `engine_placeholder::ui`). It is **CPU only**: a retained tree of widgets, layout, themes, input routing and a 2D draw list. It names no
  RHI type; the renderer (`renderer::UiRenderer`) turns the draw list into one render-graph pass.
- A **surface** owns a widget tree, a size in **logical units**, a **scale** factor, a clock, a theme and the focus. A window is only one kind of thing that feeds a surface; an offscreen
  texture (an in-world screen) is another, and tests use one without any window.
- Everything is main-thread only.

## Drawing: the canvas and the draw list

Widgets draw on a **`ui::Canvas`**, an abstract class in logical units (`ui/canvas.h`): `DrawRect`, `DrawRoundedRect` (a radius per corner), `DrawBorder`, `DrawLine`, `DrawTriangle`, `DrawImage`, `DrawText`, `DrawShadow`, a clip stack
(`PushClip`/`PopClip`) and layers (`BeginLayer(order)`/`EndLayer`). No draw-list or GPU type appears in it, so a vector canvas, a software rasteriser or an exporter can replace the implementation without touching a widget.

- **`ui::DrawListCanvas`** is the implementation that ships: it converts to physical pixels and appends to a **`ui::DrawList`** (40-byte vertices, 32-bit indices, commands). **`ui::RecordingCanvas`** (`ui/testing/`) records the calls, so a
  widget test can assert "the button drew a rounded rect with the hover colour" without reading vertices.
- **Snapping (the contract of every canvas):** rectangles round their *edges* (`round(x * scale)`, `round((x + w) * scale)`), so neighbours share an edge; thicknesses (borders, strokes) are scaled and rounded, at least one pixel: a 1-unit
  border is 1 pixel at scale 1 and 1.25, 2 at scale 2. Radii and blur are scaled, not rounded. Text is laid out by the caller in physical pixels (logical size times `Scale()`).
- **One shader, three modes** (`Vertex::flags`): solid, rectangle (signed-distance rounded box with analytic anti-aliasing; a border is the same quad with `kFlagRing`, a shadow with `kFlagShadow`), glyph coverage from an atlas page and image
  colour. A rectangle and a glyph share one pipeline, so a command splits only on clip, layer, atlas page or image; a draw that needs no texture joins the previous command.
- **Clips and layers:** a clip intersects its parent and a draw outside it makes no vertex. A layer draws above every lower layer whatever the call order and starts unclipped, so a popup is not cut by the widget that opened it; call
  `DrawList::SortLayers()` once after drawing.
- **A frame allocates nothing** once the arrays have grown: `DrawList::Clear()` keeps the capacity. Past `max_vertices` a draw is dropped and counted in `stats.overflowed`; an index never wraps.

```cpp
ui::DrawList list;
ui::DrawListCanvas canvas(&list, &atlas);   // the glyph atlas for text; null makes text count as dropped
list.Clear();
canvas.Begin(2.0f, width, height);          // scale 2, surface size in physical pixels
canvas.DrawRoundedRect({10, 10, 120, 32}, ui::CornerRadii(6), 0xFF3355CCu);
canvas.DrawBorder({10, 10, 120, 32}, ui::CornerRadii(6), 1, 0xFFFFFFFFu);
list.SortLayers();                          // then the renderer draws list.commands in order
```

More sections arrive with the days that build them (draw list, renderer, theme, tree, layout, input, widgets, popups, drag and drop, viewport, surfaces).
