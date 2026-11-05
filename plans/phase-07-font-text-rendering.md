# Phase 7 — Font & Text Rendering

**Depends on:** Phase 3 (Vulkan RHI), Phase 4 (Renderer core) — glyph
bitmaps upload as RHI textures and draw through the renderer's existing
pass/material machinery, not a separate rendering path.
**Goal:** arbitrary UTF-8 text on screen, backed by FreeType (root
`PLAN.md` assumption #17) for font parsing/glyph rasterization, reusing
the foundation string module's grapheme clustering (Days 10-12) for
correct cursor/line-break behavior — the shared text layer every later
UI need (in-game HUD, debug overlays, the Phase 8 UI system and through it
the Phase 18 editor's widgets)
builds on instead of each hand-rolling its own.
**Definition of done:** arbitrary UTF-8 text renders on screen through
the RHI at an interactive frame rate, with correct glyph shapes,
kerning/advance widths, and multi-codepoint grapheme clusters (combining
marks, emoji ZWJ sequences) each treated as one cursor-movement unit; and
the same text layout can be turned into a 3D mesh (extruded, optionally
beveled outlines of the vector glyphs) that draws through the Phase 4
renderer like any other mesh.

## Key deliverables (to be expanded into day-by-day tasks when started)

- Vendor FreeType source under `third_party/freetype/` (pinned release,
  built from source — same pattern as Catch2, `PLAN.md` assumption
  #16), not linked against a distro-provided `libfreetype.so` (see
  assumption #17 for why).
- Font loading: parse a font file via FreeType, query face metrics
  (ascent/descent/line height), enumerate/rasterize glyphs on demand.
- Glyph atlas: pack rasterized glyph bitmaps into a texture atlas,
  uploaded through the Phase 3 RHI; atlas growth/eviction policy for
  large character sets (CJK) is a stretch goal, not a Phase 7 blocker —
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
- 3D text: generate a mesh from vectorized glyphs. FreeType's outline
  API supplies each glyph's contours (quadratic/cubic Bezier curves),
  which are flattened to polylines at a caller-chosen tolerance,
  triangulated into front and back faces (contours with holes, e.g. `o`
  and `B`, must come out right), and extruded to a given depth with side
  walls, with optional bevel and smooth/flat normals. The result is a
  Phase 4 `MeshData` (positions, normals, UVs), laid out by the same text
  layout as the 2D path (advances, kerning, line breaks), so 3D text
  uploads through `GpuMesh` and draws through the material system.
  Triangulation and extrusion are our own code (no third-party
  tessellator); the flatten/triangulate/extrude steps are pure CPU
  functions and are tested without a GPU (watertightness, triangle
  winding, area against the contour area, holes). Meshes for a string are
  built on demand and cached per glyph so repeated characters share work.
  Where it shrinks the vertex count (side walls, bevels, caps of convex
  runs), glyph meshes may use triangle strips with restart indices
  (Phase 4 Day 78 topology support). Colored glyphs use the per-vertex colors added in Phase 4 Day 78 (see
  the colored-glyphs note below).

## Notes / open questions

- Full day-by-day breakdown deferred until Phase 4 is complete.
- Font hinting mode (FreeType supports several: no hinting, light,
  normal/full autohint, or the font's own bytecode hints) is a
  visual-quality tradeoff to decide empirically against real rendered
  text once this phase starts, not speculatively here.
- 3D text open questions, decided once this phase starts: bevel quality
  versus triangle count; whether glyph meshes are instanced per
  character or merged per string; curve-flattening tolerance units
  (font units vs world units); contour winding rules for fonts that mix
  conventions. Hinting is irrelevant to the 3D path (outlines are loaded
  unhinted).
- Colored glyphs (emoji) are in scope **for the 3D text path and the 2D
  path alike, in two formats only**: layered-outline `COLR` v0 (each
  color layer is a vector outline plus a palette color: 3D text builds
  one extruded mesh per layer carrying the color as a per-vertex color,
  the Phase 4 Day 78 vertex-color support; the 2D path draws the layers
  as tinted atlas glyphs) and bitmap `CBDT`/`sbix` (raster only, nothing
  to extrude: the 2D path samples an RGBA atlas, the 3D path draws a
  textured quad or thin slab through the existing UV path). `COLR` v1
  (gradients, transforms, compositing) and the `SVG` table are out of
  scope; a glyph in one of those formats falls back to its monochrome
  outline or the font's `.notdef`, and the fallback is a logged, tested
  behavior, not a crash. Plain grayscale rasterization stays the default
  and the only path that needs no color support.
- Subpixel/LCD-optimized rendering (FreeType supports it) is not a
  Phase 7 goal — this engine targets modern displays where grayscale
  antialiasing is standard; revisit only if a real quality gap shows up.
- Widget UI (layout containers, styling, menus, buttons, the viewport
  widget, input routing) is **Phase 8's** job, built on this phase's text
  layer; this phase only commits to glyph rasterization/atlasing/basic
  layout, not a UI framework.

## What Phase 4 gives this phase (Phase 4 Day 84)

- **Glyph atlases are textures in the material system**: a text material is a template whose fragment shader samples an atlas
  (`texture slot` + sampler, a parameter block for color/outline) and the atlas upload goes through the RHI staging ring
  (`UploadTexture`). The atlas texture is replaced in place with `MaterialSystem::SetTexture`, which rebuilds only that material's bind
  groups.
- **Text is drawn after the tonemap, not in the deferred pipeline.** The main pipeline is deferred (G-buffer, lighting, tonemap) and has
  no blending of transparent geometry; text and UI need an **overlay/forward pass** that runs after the tonemap pass on the final
  target (alpha blending, no depth, sorted in draw order). This phase adds that pass as a render graph pass reading the tonemap output
  and writing the target; the graph orders it after `TonemapPass` by the resource it writes.
- Glyph quads are one draw per atlas page; the per-object storage buffer (set 2) and instancing carry per-glyph data.
