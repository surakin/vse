# Phase 25 / Day 260 — COLR Layers on the 2D and 3D Paths

**Phase:** [Phase 25 — 3D Text, Large 2D Text & Colored Glyphs](../phase-25-3d-text-and-color-glyphs.md)
**Depends on:** Day 259; Days 255 and 258.

Draw layered color glyphs everywhere text draws.

## Tasks

- [ ] **2D atlas path (sizes up to the maximum):** a color glyph is drawn as its layers, each layer a normal atlas glyph tinted by the layer color (foreground index uses the draw color); the layer glyphs share the atlas entries of ordinary glyphs, so no new page type is needed; order is layer order.
- [ ] **2D mesh path (large sizes):** the same layers through the mesh batch, the color carried per vertex (`Vertex2D` gains an RGBA field; the shader multiplies it with the draw color for foreground layers by a flag).
- [ ] **3D path:** one extruded solid per layer, merged into the string's `MeshData` as `ColorVertex` (sRGB bytes) with layers separated in z by a documented tiny offset (`layer_gap_em`, default 0.002) or, with zero gap, drawn with a depth bias; a material that does not declare the vertex-color permutation is reported by the existing mismatch check; the foreground layer takes the material color.
- [ ] **Layout interaction:** advances and pen positions are those of the base glyph; the layers' bounds union is the glyph's bounds for culling.
- [ ] **Tests:** CPU: the merged 3D mesh of a three-layer glyph has three colored regions with the palette bytes and depth order; GPU per backend: 2D atlas path, 2D mesh path (a layered glyph at 200 px) and 3D path readbacks show the three palette colors at known pixels, and the foreground layer follows the draw color; a string mixing color and ordinary glyphs keeps the pen positions of the same string without color glyphs; goldens for the 2D and 3D renderings of the builder's font.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC no later than Day 265; until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/text.md` "Colored glyphs": drawing, the foreground color, limits.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** COLR v0 glyphs render in color on the atlas path, the large mesh path and the 3D path, with pen positions and culling unchanged, verified by readback on every backend.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
