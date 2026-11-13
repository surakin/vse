# Phase 27 / Day 350 — Vendor zlib & libpng, Enable PNG in FreeType

**Phase:** [Phase 27 — 3D Text, Large 2D Text & Colored Glyphs](../phase-27-3d-text-and-color-glyphs.md)
**Depends on:** Day 339; `third_party/freetype/VENDORED.md` (Phase 14 Day 175).

**Amended 2026-11-14:** zlib and libpng are first vendored by Phase 22 Day 335 (the glTF importer's PNG decoding, linked into `asset_mint` only); this day reuses those pinned sources instead of vendoring them again, adds the FreeType side (`FT_CONFIG_OPTION_USE_PNG`, the link, the hostile-strike limits and the boundary rule that now allows `asset_mint` **and** FreeType), and keeps everything else below. If Day 335 has not happened when this day starts, this day vendors them as written and Day 335 reuses them.

The user's decision (2026-11-10): vendor libpng. FreeType then decodes the PNG strikes of `CBDT` and `sbix` fonts itself. This is a deliberate, individually justified exception to the zero-third-party line, the same category as FreeType (ships inside the runtime binary), recorded in `PLAN.md` assumption #17.

## Tasks

- [ ] **Vendor zlib and libpng** under `third_party/zlib/` and `third_party/libpng/`: the latest stable releases at the time, pinned by version and SHA-256, source only (the library sources, headers, license files; no tests, contrib, docs or build scripts of upstream), `VENDORED.md` for each (version, URL, hash, license: zlib license and the libpng license, notices to ship with the FreeType credit), our `CMakeLists.txt` building one static library each with the vendored-code flags (`-w`/`/W0`, outside the root's `-Werror`), `pnglibconf.h` generated once and committed, no SIMD/assembly variants (portable C only, so every platform and the Phase 21-29 ports build the same source), allocation through libpng's default (malloc) -- FreeType's counting `FT_Memory` covers FreeType's own blocks only, which the guide states.
- [ ] **Enable PNG in FreeType:** `third_party/freetype/engine_config/engine_ftoption.h` defines `FT_CONFIG_OPTION_USE_PNG` and stops undefining `FT_CONFIG_OPTION_USE_ZLIB` where libpng needs zlib (FreeType's own gzip/`.gz` font support stays off: only the PNG path is wanted -- verify with the library test that `.gz` fonts are still refused); `third_party/freetype/CMakeLists.txt` links the two libraries and the include paths; `VENDORED.md` of FreeType updated; `PLAN.md` assumption #17 and the `CLAUDE.md` bullet "Font rendering is backed by FreeType" name libpng and zlib as vendored with it; `BUILD.md` needs no new prerequisite (source vendored) but says so.
- [ ] **Module rule:** libpng and zlib are used only by FreeType (no engine file includes their headers: `module_deps_check`/a new line in `std_usage_check`'s third-party boundary and `rhi_boundary_check`-style gate `third_party_boundary_check` or an extra rule in an existing gate, whichever Day 339 picks) so the PNG path stays one swap away; Phase 22 may add its own use then, with its own entry in `PLAN.md`.
- [ ] **Experiments:** binary-size and build-time cost on Linux (and Windows at Day 354); libpng's `png_set_*` limits (`png_set_user_limits`, chunk cache and malloc limits) that FreeType's `pngshim` leaves at defaults -- record what a hostile 100000x100000 PNG strike does (FreeType's own checks on strike size come first; test it) and whether the engine needs a size limit on `FT_Load_Glyph` of color bitmaps.
- [ ] **Tests** (`tests/text/library_test.cpp`, phase 27): `FontLibrary` reports PNG support (`FT_Library_SetLcdFilter`-style probe: load a PNG-strike glyph from the Day 348 builder font once Day 351 adds it; on this day a probe of `FT_Get_Module` / feature flags through `FontLibrary::Version()` extended with `png_support`); the FreeType version test still passes; zlib and libpng versions are reported by `FontLibrary::Version()` and pinned by a test; an allocation-failure sweep (`FailAllocationsAfter`) over a PNG glyph load added on Day 351 is noted here as a follow-up for that day.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors asan on the text tests; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC no later than Day 354; until then the day files "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `third_party/*/VENDORED.md`, `docs/guide/text.md` "Colored glyphs" (the formats FreeType reads now).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** zlib and libpng are vendored, pinned and documented, FreeType decodes PNG, the dependency exception is written into `PLAN.md` and `CLAUDE.md`, and nothing but FreeType includes their headers.

## Notes

- (filled in as the day is worked)

## Post-implementation code review

(Filled in at the end of the day.)
