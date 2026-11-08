# Phase 14 / Day 181 — The Overlay Text Pass and the Draw API

**Phase:** [Phase 14 — Font & Text Rendering](../phase-14-font-text-rendering.md)
**Depends on:** Day 180; the render graph (Phase 5).

The one call a program makes to put text on the screen, drawn after the tonemap.

## Tasks

- [ ] **`renderer::TextRenderer`** (owner of the atlas, atlas textures and the text material; per-frame vertex data in the frame resources' regions like the other per-frame buffers):
      `Draw(const TextLayout&, origin_px, color)`, `Draw(StringView, Font&, size, origin_px, color, LayoutOptions)` as the convenience call, `Clear` at frame start; draws batch by atlas page in
      call order (z-order = call order, as the Phase 5 notes say).
- [ ] **Render-graph pass** `TextPass`: reads the tonemap output, writes the target, ordered after `TonemapPass` by the resource it writes; alpha-blends glyph quads, no depth; skipped (no cost, no
      barrier) when nothing was drawn that frame; works when the renderer has no text at all (the graph is unchanged).
- [ ] **Glyph quad building:** from `PositionedGlyph` + `GlyphEntry` to four vertices (pixel coordinates to clip space with the target size, snapped to whole pixels so hinted text stays crisp), index buffer
      shared and static; 100k glyphs per frame capacity before a documented split into several draws; overflow is a counted drop, never memory corruption.
- [ ] **Sizes above the atlas maximum (interim, until Phase 16's outline-mesh path):** layout runs at the requested size; each glyph's raster comes from the maximum size and its quad is scaled up by the
      ratio (blurry but present and correctly placed), counted in `Stats` (`scaled_large_glyphs`) and logged once per size; a test draws 64 px, the maximum and 2x the maximum, checks the 2x text is as wide as the
      layout says and that the atlas holds no glyph larger than the maximum.
- [ ] **Resize and format:** target resize and `ApplySettings` changes (resolution, present mode) rebuild what depends on them; the pass supports the renderer's target formats (sRGB and linear), and the
      coverage-to-color math is checked in both.
- [ ] **Per-glyph cost:** no per-glyph heap allocation in steady state (vectors reserved, reused); a test counts allocations over 100 frames of the same text with the allocation counter of the render graph test.
- [ ] **Tests** (`tests/renderer/text_pass_test.cpp`, GPU, per backend): "Hello, world!" at 16 px drawn onto a cleared target — readback checks that pixels outside the text's bounds are untouched, that the text
      bounds contain nonzero coverage, and that a second call overlays in order; an empty frame skips the pass (pass count in `Stats`); a draw before the atlas has the glyph adds it that frame; resize.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux and Windows; then the asan and tsan flavors if the day touched lifetime, memory or concurrency code;
      quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with
      Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the day closes; a day done on one machine files
      "open until the other machine runs it" in `KNOWN-ISSUES.md` and the closing day clears it.
- [ ] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed.
- [ ] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under
      "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a program draws text with one call and the pass appears after the tonemap only when needed, with no per-glyph allocation in steady state and the pass and quad tests passing per backend.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
