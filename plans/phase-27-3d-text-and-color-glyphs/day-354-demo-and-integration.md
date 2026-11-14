# Phase 27 / Day 354 — `text3d_demo`, Settings Screen & Integration

**Phase:** [Phase 27 — 3D Text, Large 2D Text & Colored Glyphs](../phase-27-3d-text-and-color-glyphs.md)
**Depends on:** Days 345, 348, 350 and 353; `game/ui_demo` (Phase 15).

One demo that shows everything the phase adds, usable for the by-eye check and the goldens.

## Tasks

- [ ] **`game/text3d_demo`** (windowed program via `ENGINE_WINDOWED_PROGRAM`): a rotating beveled 3D string with the PBR material and a light; a 2D overlay with the same string at 24, 128, 129, 200 and 400 px (showing the switchover); a COLR line and a bitmap line from the test-font builder (written to memory at start); a text-input UI (Phase 15) edits the 3D string live; a frame counter.
- [ ] **Launch options** for the demo only (`--frames N`, `--validation`, `--rhi`, `--golden PATH` as the other demos); anything configurable in the engine stays a setting.
- [ ] **Settings screen:** the Phase 15 settings view lists `renderer.text.mesh_min_pixel_size` and `renderer.text3d.cache_bytes`; a test applies both at run time.
- [ ] **Integration tests:** the demo runs 1000 frames with `--validation` on Wayland and X11 with exit 0; a golden frame (frame 60, fixed rotation) under `tests/golden/` per backend compared with the repository's image-compare tolerance; a reset (`ApplySettings` backend-independent change and a forced device loss in the test rig) mid-run keeps the picture; the ASCII-only atlas path is unchanged (its Phase 14 goldens still pass bit-exact).
- [ ] **Cost table** in the guide: meshes built per second, the cost of a 100-glyph 3D string (build and draw), the mesh batch's cost at 1, 10, 100 large glyphs per frame, memory of the three caches.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC no later than Day 355; until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/text.md` complete ("Large text", "3D text", "Colored glyphs", "Cost", "Limits"); `docs/guide/building-and-testing.md` demo list.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the demo exercises large 2D text, 3D text and both color formats, runs clean with validation on both display servers, and has goldens.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
