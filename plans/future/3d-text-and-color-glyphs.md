# Future project — 3D Text, Large 2D Text & Colored Glyphs

**Status: a future project, not scheduled (the user, 2026-11-23: "it's not that urgent").** It was Phase 28 (Days 478-493). Its day files are written and stay as they were,
but the days are numbered 1 to 16 within the project: it takes the next phase number and the next free day numbers when it is scheduled, and its plan, `PLAN.md`'s
roadmap and the references to it are renumbered then. Nothing in the engine depends on it: the 2D text path draws text above the atlas's largest size from the largest
raster, scaled up (blurry, counted, logged once), until this project is built. Phase and day numbers below that name another phase are those phases' own.

**Depends on:** Phase 14 (Font & Text: FreeType, text layout, the 2D text path) and Phase 5 (Renderer core: `MeshData`/`GpuMesh`, the material
system, vertex colors and triangle strips from Phase 5 Day 78). Phase 17 (ECS and its scene bridge, which 3D text entities use). Phase 15 (UI) does not need it and does not wait for it.
**Split out of Phase 14 on 2026-11-08** (developer decision: Phase 14 is cut to what a "hello world" needs).
**Goal:** the text Phase 14 lays out can also be a mesh: a flat filled 2D mesh for text above Phase 14's maximum raster size, or an extruded 3D mesh; and glyphs that carry their own colors (emoji) draw on both the 2D and the 3D path.
**Definition of done:** text above `kMaxAtlasPixelSize` (Phase 14 Day 175) draws sharp at any size as filled 2D meshes from the glyph outlines, replacing Phase 14's scaled-raster stand-in (same pen positions, so nothing moves), with holes right and edges antialiased (the method is chosen at the phase start: MSAA, analytic edge coverage or a coverage ramp on the border triangles); a string becomes an extruded, optionally beveled mesh that draws through the Phase 5 renderer like any other mesh, with
holes (`o`, `B`) right, watertight, correctly wound; and a font's colored glyphs (`COLR` v0 layers and `CBDT`/`sbix` bitmaps) render in 2D and 3D, with
the unsupported formats falling back to the monochrome outline or `.notdef` in a logged, tested way.

## Days

1. [Design Spec, Decisions & Phase Setup](3d-text-and-color-glyphs/day-01-design-spec-and-setup.md)
2. [Flattening Glyph Outlines](3d-text-and-color-glyphs/day-02-flatten-outlines.md)
3. [Contour Orientation, Nesting & Overlap Removal](3d-text-and-color-glyphs/day-03-contour-cleanup.md)
4. [Triangulating Polygons with Holes](3d-text-and-color-glyphs/day-04-triangulation.md)
5. [Large 2D Glyph Meshes: Cache & Edge Ramp](3d-text-and-color-glyphs/day-05-large-2d-glyph-meshes.md)
6. [Large 2D Text on the GPU](3d-text-and-color-glyphs/day-06-large-2d-text-rendering.md)
7. [Extruding Glyph Geometry](3d-text-and-color-glyphs/day-07-extrusion.md)
8. [Beveled Extrusion](3d-text-and-color-glyphs/day-08-bevel.md)
9. [3D Text Meshes Through the Renderer](3d-text-and-color-glyphs/day-09-text-3d-mesh-and-rendering.md)
10. [Test Font Builder & COLR v0 Layers](3d-text-and-color-glyphs/day-10-test-fonts-and-colr.md)
11. [COLR Layers on the 2D and 3D Paths](3d-text-and-color-glyphs/day-11-colr-on-2d-and-3d.md)
12. [Vendor zlib & libpng, Enable PNG in FreeType](3d-text-and-color-glyphs/day-12-vendor-libpng.md)
13. [Color Bitmap Glyphs: `CBDT` and `sbix` through FreeType](3d-text-and-color-glyphs/day-13-color-bitmap-glyphs.md)
14. [RGBA Atlas Pages & Bitmap Color Glyphs on 2D and 3D](3d-text-and-color-glyphs/day-14-rgba-atlas-and-bitmap-drawing.md)
15. [`text3d_demo`, Settings Screen & Integration](3d-text-and-color-glyphs/day-15-demo-and-integration.md)
16. [Docs, Windows PC, Closing Full Run & Phase Review](3d-text-and-color-glyphs/day-16-docs-closing-run-and-review.md)

Order reasoning: geometry first, pure CPU and testable without a GPU (flatten 2, cleanup 3, triangulate 4), because both consumers share it; the large 2D path (5-6) comes before 3D because it replaces a Phase 14 stand-in and is smaller; extrusion and bevel (7-8) precede the renderer integration (9); colors start with COLR (10-11), which needs no new decoder, and the bitmap formats (12-14) come last because they need libpng vendored behind FreeType.

## Key deliverables (the original list; the days above break it down)

Carried over unchanged from the Phase 14 plan as it stood before the split:

- **3D text:** FreeType's outline API supplies each glyph's contours (quadratic/cubic Beziers), flattened to polylines at a caller-chosen tolerance,
  triangulated into front and back faces (contours with holes must come out right), extruded to a given depth with side walls, with optional bevel
  and smooth or flat normals. The result is a Phase 5 `MeshData` (positions, normals, UVs), laid out by Phase 14's text layout (advances, kerning, line
  breaks), uploaded through `GpuMesh`, drawn through the material system. Triangulation and extrusion are our own code (no third-party tessellator);
  flatten, triangulate and extrude are pure CPU functions tested without a GPU (watertightness, winding, area against the contour area, holes). Meshes
  are built on demand and cached per glyph. Triangle strips with restart indices (Phase 5 Day 78) where they shrink the vertex count.
- **Large 2D text (developer decision 2026-11-08):** glyphs above Phase 14's maximum atlas size draw as filled 2D meshes: the outline flattened at a tolerance proportional to the pixel size, triangulated with holes
  (nonzero/even-odd per the font's winding), cached per (font, glyph, tolerance bucket) and scaled to the requested size by the vertex transform, so one mesh serves every large size and animated sizes cost nothing
  per frame. It is the same flatten-and-triangulate code the 3D text faces use (one implementation, two consumers); the overlay pass gains a mesh batch beside its atlas quads, and the text renderer picks the path
  per glyph by size. The scaled-raster fallback of Phase 14 Day 181 is removed in this phase, with its `Stats` counter.
- **Colored glyphs, in two formats only:** layered-outline `COLR` v0 (3D text builds one extruded mesh per layer carrying the palette color as a
  per-vertex color; the 2D path draws the layers as tinted atlas glyphs) and bitmap `CBDT`/`sbix` (raster only: the 2D path samples an RGBA atlas, the
  3D path draws a textured quad or thin slab). `COLR` v1 and the `SVG` table are out of scope: such a glyph falls back to its monochrome outline or
  `.notdef`, a logged, tested behavior.

## Notes / open questions

- Open, decided once this phase starts: antialiasing of the 2D mesh edges; the size at which the mesh path takes over from the atlas (at the atlas maximum, or lower if meshes look better earlier); bevel quality versus triangle count; instanced per character or merged per string; curve-flattening tolerance
  units (font units or world units); contour winding rules for fonts that mix conventions. Hinting is irrelevant to the 3D path (outlines load unhinted).
- Phase 14 must keep the seams this phase needs (recorded in its design spec): the font object exposes outline loading as well as bitmap rendering,
  the layout produces glyph ids and pen positions independent of how a glyph is drawn, and the atlas can hold an RGBA page later.
- Day files written 2026-11-10 (Days 1-16). Day 1 settles the open questions above with the planner's defaults and experiments. **User decision (2026-11-10): vendor libpng** (with zlib) behind FreeType, so `CBDT`/`sbix` PNG strikes decode through FreeType; a deliberate exception to the zero-third-party line, written into `PLAN.md` assumption #17 and `CLAUDE.md` on Day 12.
- Phase 16's Day 219 (Windows PC) is still open; Day 16 also needs the Windows PC, for DXIL minting and the Windows closing run.
