# Phase 14 — Font & Text Rendering

**Depends on:** Phase 4 (Vulkan RHI), Phase 5 (Renderer core), Phase 8 (RHI reset), Phase 13 (DX12, per-backend GPU tests) — glyph
bitmaps upload as RHI textures and draw through the renderer's existing
pass/material machinery, not a separate rendering path.
**Goal:** arbitrary UTF-8 text on screen, backed by FreeType (root
`PLAN.md` assumption #17) for font parsing/glyph rasterization, reusing
the foundation string module's grapheme clustering (Days 10-12) for
correct cursor/line-break behavior — the shared text layer every later
UI need (in-game HUD, debug overlays, the Phase 15 UI system and through it
the Phase 32 editor's widgets)
builds on instead of each hand-rolling its own.
**Scope cut (2026-11-08, developer decision):** "a simple hello world is enough until the UI is implemented". 3D text and colored glyphs (emoji) moved to
[the 3D text project](future/3d-text-and-color-glyphs.md); this phase keeps only the 2D path, plus grapheme-aware cursor placement and hit-testing because the
Phase 15 UI's text fields need them.
**Definition of done:** the string "Hello, world!" (and arbitrary UTF-8: accents, CJK from a font that has them, a combining mark, an emoji ZWJ sequence
that renders as its component glyphs or `.notdef`, never a crash) draws on screen from a font file through the RHI at an interactive frame rate, in an
overlay pass after the tonemap, on Vulkan (Linux, Windows) and Direct3D 12 (Windows), with golden images; kerning and advance widths are right;
glyphs are rasterized up to a documented maximum pixel size (larger text still draws, scaled from the maximum raster, until the 3D text project draws it as outline meshes); the cursor moves, and a point maps to a byte offset and back, by grapheme cluster (combining marks and ZWJ sequences are one unit); the glyph atlas and
the text pipeline survive an RHI reset (CLAUDE.md "RHI can be reset at run time").

**Status: complete on Linux (2026-11-09), Days 175-183.** The Windows PC still has to build and run everything and mint DXIL for the text shaders (`KNOWN-ISSUES.md`).

## Days (Days 175-183; the day files carry the sub-tasks)

175. [Day 175 — Design spec, FreeType vendored, the font, the module](phase-14-font-text-rendering/day-175-design-spec-and-freetype.md)
176. [Day 176 — Font faces: loading, metrics, glyph lookup, kerning](phase-14-font-text-rendering/day-176-font-face.md)
177. [Day 177 — Glyph rasterization and the atlas packer (CPU)](phase-14-font-text-rendering/day-177-glyph-raster-and-atlas.md)
178. [Day 178 — Text layout](phase-14-font-text-rendering/day-178-text-layout.md)
179. [Day 179 — Cursor placement and hit-testing](phase-14-font-text-rendering/day-179-cursor-and-hit-testing.md)
180. [Day 180 — The atlas texture, the text pipeline and shaders](phase-14-font-text-rendering/day-180-atlas-texture-and-text-material.md)
181. [Day 181 — The overlay text pass and the draw API](phase-14-font-text-rendering/day-181-overlay-text-pass.md)
182. [Day 182 — Hello world in the demos, reset and loss, golden images](phase-14-font-text-rendering/day-182-hello-world-demo-and-golden.md)
183. [Day 183 — Guide, benchmark, closing runs, phase review](phase-14-font-text-rendering/day-183-docs-and-closing-run.md)

## Key deliverables

- Vendor FreeType source under `third_party/freetype/` (pinned release,
  built from source — same pattern as Catch2, `PLAN.md` assumption
  #16), not linked against a distro-provided `libfreetype.so` (see
  assumption #17 for why).
- Font loading: parse a font file via FreeType, query face metrics
  (ascent/descent/line height), enumerate/rasterize glyphs on demand.
- Glyph atlas: pack rasterized glyph bitmaps into a texture atlas,
  uploaded through the Phase 4 RHI; atlas growth/eviction policy for
  large character sets (CJK) is a stretch goal, not a Phase 14 blocker —
  a single generously-sized atlas per font/size is an acceptable v1.
- Text layout: turn a `StringView` into a sequence of positioned glyph
  quads — advance widths from FreeType, line breaking at whitespace/
  grapheme-cluster boundaries (foundation string module, Days 10-12),
  no complex-script shaping (see `PLAN.md` assumption #17's HarfBuzz
  note — explicitly out of scope for this phase).
- A minimal text-draw call integrated into the Phase 5 renderer/material
  system (textured quads sampling the glyph atlas, alpha-blended).
- Cursor placement / hit-testing (byte offset <-> screen position) built
  on grapheme cluster boundaries, not raw codepoints or bytes — needed
  by any future text-input UI (editor, in-game chat/console, etc.).
- **Seams for the 3D text project (kept, not built):** the font object also exposes outline access; the layout yields glyph ids and pen positions independent of how a
  glyph is drawn; the atlas can hold an RGBA page later. The design spec (Day 175) records each.

## Notes / open questions

- The day-by-day breakdown is written (Days 175-183, 2039-11-8); the phase may start once it is committed (CLAUDE.md).
- Font hinting mode (FreeType supports several: no hinting, light,
  normal/full autohint, or the font's own bytecode hints) is a
  visual-quality tradeoff to decide empirically against real rendered
  text once this phase starts, not speculatively here.
- 3D text and colored glyphs: the 3D text project (their open questions moved with them).
- Subpixel/LCD-optimized rendering (FreeType supports it) is not a
  Phase 14 goal — this engine targets modern displays where grayscale
  antialiasing is standard; revisit only if a real quality gap shows up.
- Widget UI (layout containers, styling, menus, buttons, the viewport
  widget, input routing) is **Phase 15's** job, built on this phase's text
  layer; this phase only commits to glyph rasterization/atlasing/basic
  layout, not a UI framework.

## What Phase 5 gives this phase (Phase 5 Day 84)

- **Glyph atlases are textures in the material system** (amended on Day 180: `TextRenderer` owns its pipeline on the raw RHI instead, like the other passes; the atlas pages are `GpuTexture`-style Recreatable owners): a text material is a template whose fragment shader samples an atlas
  (`texture slot` + sampler, a parameter block for color/outline) and the atlas upload goes through the RHI staging ring
  (`UploadTexture`). The atlas texture is replaced in place with `MaterialSystem::SetTexture`, which rebuilds only that material's bind
  groups.
- **Text is drawn after the tonemap, not in the deferred pipeline.** The main pipeline is deferred (G-buffer, lighting, tonemap) and has
  no blending of transparent geometry; text and UI need an **overlay/forward pass** that runs after the tonemap pass on the final
  target (alpha blending, no depth, sorted in draw order). This phase adds that pass as a render graph pass reading the tonemap output
  and writing the target; the graph orders it after `TonemapPass` by the resource it writes.
- Glyph quads are one draw per atlas page; the per-object storage buffer (set 2) and instancing carry per-glyph data.
