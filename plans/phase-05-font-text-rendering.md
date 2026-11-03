# Phase 5 — Font & Text Rendering

**Depends on:** Phase 3 (Vulkan RHI), Phase 4 (Renderer core) — glyph
bitmaps upload as RHI textures and draw through the renderer's existing
pass/material machinery, not a separate rendering path.
**Goal:** arbitrary UTF-8 text on screen, backed by FreeType (root
`PLAN.md` assumption #17) for font parsing/glyph rasterization, reusing
the foundation string module's grapheme clustering (Days 10-12) for
correct cursor/line-break behavior — the shared text layer every later
UI need (in-game HUD, debug overlays, the Phase 6 UI system and through it
the Phase 18 editor's widgets)
builds on instead of each hand-rolling its own.
**Definition of done:** arbitrary UTF-8 text renders on screen through
the RHI at an interactive frame rate, with correct glyph shapes,
kerning/advance widths, and multi-codepoint grapheme clusters (combining
marks, emoji ZWJ sequences) each treated as one cursor-movement unit.

## Key deliverables (to be expanded into day-by-day tasks when started)

- Vendor FreeType source under `third_party/freetype/` (pinned release,
  built from source — same pattern as Catch2, `PLAN.md` assumption
  #16), not linked against a distro-provided `libfreetype.so` (see
  assumption #17 for why).
- Font loading: parse a font file via FreeType, query face metrics
  (ascent/descent/line height), enumerate/rasterize glyphs on demand.
- Glyph atlas: pack rasterized glyph bitmaps into a texture atlas,
  uploaded through the Phase 3 RHI; atlas growth/eviction policy for
  large character sets (CJK) is a stretch goal, not a Phase 5 blocker —
  a single generously-sized atlas per font/size is an acceptable v1.
- Text layout: turn a `StringView` into a sequence of positioned glyph
  quads — advance widths from FreeType, line breaking at whitespace/
  grapheme-cluster boundaries (foundation string module, Days 10-12),
  no complex-script shaping (see `PLAN.md` assumption #17's HarfBuzz
  note — explicitly out of scope for this phase).
- A minimal text-draw call integrated into the Phase 4 renderer/material
  system (textured quads sampling the glyph atlas, alpha-blended).
- Cursor placement / hit-testing (byte offset <-> screen position) built
  on grapheme cluster boundaries, not raw codepoints or bytes — needed
  by any future text-input UI (editor, in-game chat/console, etc.).

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 4 is complete.
- Font hinting mode (FreeType supports several: no hinting, light,
  normal/full autohint, or the font's own bytecode hints) is a
  visual-quality tradeoff to decide empirically against real rendered
  text once this phase starts, not speculatively here.
- Colored glyphs (emoji, via `CBDT`/`COLR`/`SVG` font tables) are out of
  scope for Phase 5 — monochrome/grayscale glyph rasterization only.
  Revisit if a real use case needs emoji rendering.
- Subpixel/LCD-optimized rendering (FreeType supports it) is not a
  Phase 5 goal — this engine targets modern displays where grayscale
  antialiasing is standard; revisit only if a real quality gap shows up.
- Widget UI (layout containers, styling, menus, buttons, the viewport
  widget, input routing) is **Phase 6's** job, built on this phase's text
  layer; this phase only commits to glyph rasterization/atlasing/basic
  layout, not a UI framework.
