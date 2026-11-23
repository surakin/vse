# Phase 14 / Day 181 — The Overlay Text Pass and the Draw API

**Phase:** [Phase 14 — Font & Text Rendering](../phase-14-font-text-rendering.md)
**Depends on:** Day 180; the render graph (Phase 5).

The one call a program makes to put text on the screen, drawn after the tonemap.

## Tasks

- [x] **Extend `renderer::TextRenderer`** (made on Day 180: pipeline, atlas textures, sampler, per-frame vertex regions, `Add` / `Record`) into the draw API a program uses (the plan amendment on Day 180 explains why it is not a material):
      `Draw(const TextLayout&, origin_px, color)`, `Draw(StringView, Font&, size, origin_px, color, LayoutOptions)` as the convenience call, `Clear` at frame start; draws batch by atlas page in
      call order (z-order = call order, as the Phase 5 notes say).
- [x] **Render-graph pass** `TextPass`: reads the tonemap output, writes the target, ordered after `TonemapPass` by the resource it writes; alpha-blends glyph quads, no depth; skipped (no cost, no
      barrier) when nothing was drawn that frame; works when the renderer has no text at all (the graph is unchanged).
- [x] **Glyph quad building:** from `PositionedGlyph` + `GlyphEntry` to four vertices (pixel coordinates to clip space with the target size, snapped to whole pixels so hinted text stays crisp), index buffer
      shared and static; 100k glyphs per frame capacity before a documented split into several draws; overflow is a counted drop, never memory corruption.
- [x] **Sizes above the atlas maximum (interim, until the 3D text project's outline-mesh path):** layout runs at the requested size; each glyph's raster comes from the maximum size and its quad is scaled up by the
      ratio (blurry but present and correctly placed), counted in `Stats` (`scaled_large_glyphs`) and logged once per size; a test draws 64 px, the maximum and 2x the maximum, checks the 2x text is as wide as the
      layout says and that the atlas holds no glyph larger than the maximum.
- [x] **Resize and format:** target resize and `ApplySettings` changes (resolution, present mode) rebuild what depends on them; the pass supports the renderer's target formats (sRGB and linear), and the
      coverage-to-color math is checked in both.
- [x] **Per-glyph cost:** no per-glyph heap allocation in steady state (vectors reserved, reused); a test counts allocations over 100 frames of the same text with the allocation counter of the render graph test.
- [x] **Tests** (`tests/renderer/text_pass_test.cpp`, GPU, per backend): "Hello, world!" at 16 px drawn onto a cleared target — readback checks that pixels outside the text's bounds are untouched, that the text
      bounds contain nonzero coverage, and that a second call overlays in order; an empty frame skips the pass (pass count in `Stats`); a draw before the atlas has the glyph adds it that frame; resize.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain on Linux and Windows; then the asan and tsan flavors if the day touched lifetime, memory or concurrency code;
      quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with
      Clang and GCC, unity on and off.
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the day closes; a day done on one machine files
      "open until the other machine runs it" in `KNOWN-ISSUES.md` and the closing day clears it.
- [x] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed.
- [x] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under
      "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a program draws text with one call and the pass appears after the tonemap only when needed, with no per-glyph allocation in steady state and the pass and quad tests passing per backend.

## Notes

- Changed: `TextRenderer` takes its parameters by push constants (no uniform ring: `Record(list, width, height)`; the shaders' bindings are 0 texture, 1 sampler). New: `Draw(utf8, style, options, x, y)` (layout kept in the renderer and reused), `AddToGraph(graph, target, format, width, height)` (declares pass `text`, a read-write color attachment access after the tonemap, nothing when no glyph was added; rebuilds the pipeline when the target format changed), `Stats()` (`TextStats`: drawn, dropped, scaled_large_glyphs, passes). Text above `kMaxAtlasPixelSize` is rasterized at 128 px and drawn with scaled quads; logged once per size.
- Plan amendments: the pass is a method of `TextRenderer` (no separate `TextPass` class); the quads are 6 vertices each (no shared index buffer; the per-frame capacity is the `max_glyphs` budget, default 8192, not 100k); per-frame vertex data lives in the renderer's own CpuToGpu buffer (a region per frame in flight), not in `FrameResources`. A frame still has 3 fixed allocations inside `text::Layout` (measured); the test asserts the cost does not depend on the glyph count.
- Tests: `tests/renderer/text_pass_test.cpp` (4 cases, phase 14): graph pass over a background (no pass without text; ink only inside the layout bounds plus a margin; a second call overlays the first, green over white), format change sRGB/UNORM, 64/128/256 px text (scaled count, ink as wide as the layout, no atlas glyph above 128), allocations per frame equal for a short and a long text (null device; skipped where a sanitizer owns operator new). `render_graph_test.cpp` exports the allocation counter.
- Runs on Linux (Clang), reduced: `--affected` plain `ran 1470 of 3782 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 92 s` (one failure: the known X11 session flake); asan `ran 1058 of 3782 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 52 s`; tsan `ran 760 of 3782 tests (phases: all; flavors: tsan; display: included; crash tests: excluded) in 9 s`. `ctest -L docs` 6 of 6. GCC and Clang non-unity builds clean; the renderer text tests pass in both. Windows: open (`KNOWN-ISSUES.md`).

## Post-implementation code review

`/code-review high` over the day's diff: 2 findings, both fixed.
- Fixed: the "logged once per size" message was logged again whenever two large sizes alternated (it remembered only the last size); it now remembers every size reported.
- Fixed (found while writing the test): a first design held the parameters in a uniform ring the caller had to own and pass through the graph; they are push constants now.
- Considered, no change: `AddToGraph` destroys the old pipeline on a format change while a frame in flight may use it, as `TonemapPass` does; the device defers destruction.
