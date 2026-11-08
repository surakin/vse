# Phase 16 — 3D Text & Colored Glyphs

**Depends on:** Phase 14 (Font & Text: FreeType, text layout, the 2D text path) and Phase 5 (Renderer core: `MeshData`/`GpuMesh`, the material
system, vertex colors and triangle strips from Phase 5 Day 78). Phase 15 (UI) does not need it and does not wait for it.
**Split out of Phase 14 on 2026-11-08** (developer decision: Phase 14 is cut to what a "hello world" needs).
**Goal:** the text Phase 14 lays out can also be a 3D mesh, and glyphs that carry their own colors (emoji) draw on both the 2D and the 3D path.
**Definition of done:** a string becomes an extruded, optionally beveled mesh that draws through the Phase 5 renderer like any other mesh, with
holes (`o`, `B`) right, watertight, correctly wound; and a font's colored glyphs (`COLR` v0 layers and `CBDT`/`sbix` bitmaps) render in 2D and 3D, with
the unsupported formats falling back to the monochrome outline or `.notdef` in a logged, tested way.

## Key deliverables (to be expanded into day-by-day tasks when started)

Carried over unchanged from the Phase 14 plan as it stood before the split:

- **3D text:** FreeType's outline API supplies each glyph's contours (quadratic/cubic Beziers), flattened to polylines at a caller-chosen tolerance,
  triangulated into front and back faces (contours with holes must come out right), extruded to a given depth with side walls, with optional bevel
  and smooth or flat normals. The result is a Phase 5 `MeshData` (positions, normals, UVs), laid out by Phase 14's text layout (advances, kerning, line
  breaks), uploaded through `GpuMesh`, drawn through the material system. Triangulation and extrusion are our own code (no third-party tessellator);
  flatten, triangulate and extrude are pure CPU functions tested without a GPU (watertightness, winding, area against the contour area, holes). Meshes
  are built on demand and cached per glyph. Triangle strips with restart indices (Phase 5 Day 78) where they shrink the vertex count.
- **Colored glyphs, in two formats only:** layered-outline `COLR` v0 (3D text builds one extruded mesh per layer carrying the palette color as a
  per-vertex color; the 2D path draws the layers as tinted atlas glyphs) and bitmap `CBDT`/`sbix` (raster only: the 2D path samples an RGBA atlas, the
  3D path draws a textured quad or thin slab). `COLR` v1 and the `SVG` table are out of scope: such a glyph falls back to its monochrome outline or
  `.notdef`, a logged, tested behavior.

## Notes / open questions

- Open, decided once this phase starts: bevel quality versus triangle count; instanced per character or merged per string; curve-flattening tolerance
  units (font units or world units); contour winding rules for fonts that mix conventions. Hinting is irrelevant to the 3D path (outlines load unhinted).
- Phase 14 must keep the seams this phase needs (recorded in its design spec): the font object exposes outline loading as well as bitmap rendering,
  the layout produces glyph ids and pen positions independent of how a glyph is drawn, and the atlas can hold an RGBA page later.
- Full day-by-day breakdown is written when Phase 15 is complete (policy: every day gets its file before the phase starts).
