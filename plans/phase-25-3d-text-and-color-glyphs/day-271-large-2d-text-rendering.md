# Phase 25 / Day 271 — Large 2D Text on the GPU

**Phase:** [Phase 25 — 3D Text, Large 2D Text & Colored Glyphs](../phase-25-3d-text-and-color-glyphs.md)
**Depends on:** Day 270; `renderer::TextRenderer` (Phase 14 Days 180-181).

Draw the Day 270 meshes in the overlay pass and remove Phase 14's scaled-raster stand-in.

## Tasks

- [ ] **Slang shaders** `shaders/text_mesh.vert.slang` / `.frag.slang`: vertex stage moves fringe vertices by `fringe_px * normal` in pixel space and passes alpha; fragment multiplies the text color's alpha by it; premultiplied-alpha blending as the atlas path; SPIR-V minted and committed; **DXIL waits for the Windows PC** (`KNOWN-ISSUES.md`, as for the text shaders on Day 180); Metal `main` rename and Y flip notes followed (`shaders/README.md`).
- [ ] **`TextRenderer` mesh batch:** the per-frame vertex and index regions (one per frame in flight, like the quad region), a second pipeline, a `Draw` that routes by size: a glyph at `pixel_size > mesh_min_pixel_size` (default `kMaxAtlasPixelSize`) goes to the mesh batch, others to the atlas; z-order stays call order (a draw list records the interleaving of batches); overflow is a counted drop like the quads.
- [ ] **Remove the scaled-raster fallback:** `scaled_large_glyphs` leaves `TextStats`, the logged-once-per-size message and the 128 px raster path in `text_renderer.cpp` are deleted; `docs/guide/text.md` and `KNOWN-ISSUES.md` ("largest rasterized size is 128 px") are updated; `Font::Render`/`GlyphAtlas` still refuse sizes above the maximum (the atlas holds nothing larger).
- [ ] **Setting:** `renderer.text.mesh_min_pixel_size` declared with `settings::Registry` (env and flag names), applied at run time by `ApplySettings` (cheap class: it changes routing only); lowering it re-routes already cached text without rebuilding the atlas.
- [ ] **Reset safety:** the mesh batch's buffers and pipeline implement `Release`/`Recreate`; the CPU `GlyphMeshCache` is the source; a test through `tests/renderer/reset_rig.h` compares `Device::Resources()` counts before and after.
- [ ] **GPU tests** (`tests/renderer/text_mesh_test.cpp`, per backend): 160 px and 512 px 'Hello' drawn onto a cleared target; pixels outside the layout bounds untouched; coverage inside the bounds matches the software rasterizer within a tolerance; the same string at 128 px (atlas) and 129 px (mesh) has the same bounds within one pixel (nothing moves); two overlapping draws blend in call order; text at a non-integer origin has partial-coverage edges; a golden PGM of 'Hamburg' at 200 px committed under `tests/renderer/golden/`.
- [ ] **Quality/cost check for the switchover** at 96, 128, 160 px: side-by-side PGMs of the atlas and mesh paths and the per-frame CPU/GPU cost (the numbers back the default of the setting; if the mesh path is clearly better below 128 px the spec is amended).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC no later than Day 281; until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/text.md` "Large text" finished (cost, setting, limits); `docs/guide/render-settings.md` gets the new setting.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** text of any size above the atlas maximum draws sharp as antialiased meshes in the overlay pass, the scaled-raster stand-in and its counter are gone, the path switch is a run-time setting, and the tests pass per backend (DXIL excepted until the Windows PC).

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
