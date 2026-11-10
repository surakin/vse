# Phase 14 / Day 175 — Design Spec, FreeType Vendored, the Font, the Module

**Phase:** [Phase 14 — Font & Text Rendering](../phase-14-font-text-rendering.md)
**Depends on:** the Phase 13 close (Day 174).

Settle the design with experiments before any text code, vendor FreeType the way the project vendors libraries, pick the one font the engine ships, and give the new code its place in the layers.

## Tasks

- [x] **Design spec** `docs/superpowers/specs/2026-11-NN-text-design.md` (written by this day, confirmed or amended by experiments):
      - the module split: `engine/text` (CPU only: font faces, rasterization, atlas packing, layout, cursor; depends on foundation, FreeType only inside its implementation
        files) and the renderer side (atlas texture owner, text material, overlay pass) in `engine/renderer`;
      - the data types (`text::Font`, `GlyphId`, `GlyphMetrics`, `GlyphBitmap`, `AtlasPage`, `TextLayout`, `PositionedGlyph`, `TextRun`) with their ownership;
      - **what a glyph is here:** one Unicode code point maps to one glyph through the font's cmap; no shaping (HarfBuzz stays out, `PLAN.md` assumption #17); a grapheme cluster
        of several code points draws as its component glyphs and moves the cursor as one unit;
      - sizes: pixel size per `Font` instance and per atlas; whether the atlas is keyed by (font, pixel size, glyph) and how HiDPI scale enters (a size in pixels the caller
        computes; no hidden scale);
      - **size policy (developer decision 2026-11-08):** the atlas rasterizes glyphs up to a **maximum pixel size** (`kMaxAtlasPixelSize`; the value is fixed by the Day 175 measurements of atlas area and
        rasterization time per glyph, starting from 128 px). Metrics, layout and outlines work at any size up to a separate sanity limit (2048 px). **Above the maximum, text is meant to draw as filled 2D
        meshes built from the glyph outlines** (the same flatten-and-triangulate code Phase 25's 3D text uses for its faces), so that path is built in Phase 25; until then the text renderer draws such
        glyphs from the maximum-size raster scaled up (blurry, counted in `Stats`, logged once), never skipped and never an atlas flood. Sizes between the minimum and maximum are used as requested (no
        quantization in v1); the pen positions the layout produces do not depend on which path draws a glyph, so switching paths later moves nothing;
      - the atlas: R8 coverage pages, a shelf packer, page size, what happens when a page is full (a new page, one draw per page, as Phase 5 Day 84 says), eviction **not** in v1;
      - **reset and loss (CLAUDE.md RHI rule):** the atlas keeps its CPU pages, registers with `renderer::DeviceResources` at its tier, implements `Release`/`Recreate`, and
        never caches a handle past a frame;
      - the seams Phase 25 needs: outline access on the font, glyph ids and pen positions independent of drawing, an atlas that can add an RGBA page;
      - hinting: the candidates (none, light, normal, the font's bytecode) and how the choice is made (rendered samples at 12, 16, 24, 48 px compared by eye and by a coverage
        test), recorded with the pictures' file names;
      - the settings the phase adds, if any (CLAUDE.md "Anything configurable is a setting"): which are settings and which are launch-only options of the demos.
- [x] **Experiments for the spec:** (a) FreeType built with the minimum modules (TrueType/CFF/autofit only; no zlib, bzip2, png, brotli, harfbuzz) compiles warning-free under our flags or is
      built with the vendored-code flags (`third_party/` is excluded from `-Werror`); (b) the binary-size and build-time cost; (c) rasterizing "Hello, world!" at four sizes with each hinting mode
      into PGM files in the scratchpad; (d) rasterization time and atlas area per glyph at 16, 32, 64, 128, 256 and 512 px, to fix `kMaxAtlasPixelSize`.
- [x] **Vendor FreeType** under `third_party/freetype/`: the latest stable release at the time, pinned by version and SHA-256, source only, `VENDORED.md` (version, source URL, hash,
      license choice FTL or GPLv2 — the FreeType License, recorded, with the notice file shipped), the modules enabled in `ftmodule.h`/`ftoption.h` (our config header, the upstream files
      untouched), and a CMake target `freetype` built from source with the same pattern as Catch2 (`PLAN.md` assumption #16). Builds on Linux, MSVC and clang-cl (the Windows PC checks).
- [x] **The font the engine ships:** one permissively licensed font (SIL OFL or Apache, with Latin, Latin Extended, Greek, Cyrillic; recorded with its license file under
      `assets/fonts/` and `third_party/` notices), chosen against the checklist: coverage for the demo strings, kerning table present, file size, license. A second font with CJK coverage
      is used for tests only if one with a small file size exists; otherwise the CJK case is a `.notdef` test.
- [x] **FreeType boundary gate:** `tools/platform_neutral_check/library_boundary.txt` gains `ft2build.h` / `freetype/*.h`, allowed only in `engine/text/font_freetype.cpp` (and its test double);
      the gate's planted test is extended. FreeType is allocated through our allocator (`FT_Memory`), so the engine's allocation accounting and the raw-`new` gate stay true.
- [x] **Module placement:** `ARCHITECTURE.md` gets the row for `engine/text` (it joins layer 10 beside `jobs`, depending on the foundation only; no layer renumbers), `tools/module_deps_check/modules.txt` the mapping, `docs/reference/modules.txt` the module (documentation strict rules for it), `BUILD.md` the FreeType entry
      (vendored: nothing to install), `PLAN.md` assumption #17 the pinned version and the license decision.
- [x] **Skeleton:** `engine/text/CMakeLists.txt`, an empty `text` library linked by nothing yet, `tests/text/` with a first test (the FreeType library initializes through our allocator and
      reports its version) labeled phase 14 in `tests/phases.txt` with `flavors 14 ...` lines for Linux and Windows.
- [x] `plans/phase-14-font-text-rendering.md` and `PLAN.md` amended if an experiment changes a decision; `CURRENT_PHASE` changes to 14 in the commit that starts the phase (this one).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain and the Windows build; then the asan and tsan flavors if the day touched lifetime, memory or concurrency code;
      quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with
      Clang and GCC, unity on and off. *(Linux done; the Windows build waits for the PC, see below.)*
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** shared code is built and tested on the Windows PC before the day closes; a day done on one machine files
      "open until the other machine runs it" in `KNOWN-ISSUES.md` and the closing day clears it.
- [x] **Documentation:** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed.
- [x] **End-of-day review:** `/code-review high` over the day's diff; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the outcome under
      "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the spec is committed with its experiments (the PGM samples named in it), FreeType builds from source on Linux and Windows and initializes through our allocator, the font and its license are in the repository, the boundary gate and the module rows are in, and `CURRENT_PHASE` is 14. Pages touched: `BUILD.md`, `ARCHITECTURE.md`, `docs/reference/modules.txt`.

## Notes

- Design spec: `docs/superpowers/specs/2026-11-08-text-design.md` (experiments a-d, `kMaxAtlasPixelSize = 128`, light hinting; samples in `2026-11-08-text-hinting-samples.png`).
- FreeType 2.15.0 vendored (FTL), built as the static library `freetype` with `engine_ftoption.h` (no zlib, no SVG) and the module list in `ftmodule.h`; named only in `engine/text/font_freetype.cpp`,
  enforced by the `platform_neutral_check` boundary (now also for direct `<freetype/...>` includes).
- Font: Liberation Sans Regular under `assets/fonts/` with its license.
- Runs on Linux (Clang): `scripts/test-phase.sh --affected`: `ran 1411 of 3612 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 57 s`;
  `--affected --flavor asan`: `ran 999 of 3612 tests (phases: all; flavors: asan; display: included; crash tests: excluded) in 42 s`;
  `--affected --flavor tsan`: `ran 708 of 3612 tests (phases: all; flavors: tsan; display: included; crash tests: excluded) in 12 s`. Reduced runs, not the full suite.
  `ctest -L docs`: 6 of 6 (reference 4502 of 4502). Non-unity (Clang) and GCC (unity) builds of `text` and `text_tests` are warning-free and the two text tests pass.
- **Open until the Windows PC runs it:** the MSVC and clang-cl builds of `freetype`, `engine/text` and `tests/text`, and `platform_neutral_check` there (filed in `KNOWN-ISSUES.md`).

## Post-implementation code review

`/code-review high` over the day's uncommitted diff: **8 findings, all fixed** (none declined).

1. `platform_neutral_check` matched only `ft2build.h`, so `#include <freetype/freetype.h>` passed. Fixed: the library rule also maps `freetype/*.h` includes to the FreeType boundary; the planted fixture
   gained that line and the expected count is 25.
2. No `KNOWN-ISSUES.md` entry for the Windows-unverified work. Fixed: entry added.
3. `VENDORED.md` and `PLAN.md` claimed a notice file that does not exist. Fixed: the credit requirement is stated, and the missing notices file is filed in `KNOWN-ISSUES.md`.
4. The spec reported an unmeasured cost experiment. Fixed: measured (`libfreetype.a` 2.9 MB, 1.4 s single-threaded build, `libtext.a` 46 KB, unoptimized configuration); the optimized size is filed as unmeasured.
5. Day 177 compared against PGM files that were only in the scratchpad. Fixed: Day 177 commits its own goldens under `tests/text/golden/`.
6. The Day 175 plan text said the layers renumber. Fixed: text joins layer 10, nothing renumbers.
7. `ProbeLibrary`'s `Fails:` label contradicted the code when shutdown fails. Fixed: the documentation states both cases (also `Returns:`); the reallocation copy loop became `std::memcpy`.
8. The spec said one `FT_Library` per `Font`. Fixed: one per `FontLibrary` (new task in Day 176), shared by its fonts.

The findings were documentation and gate gaps; no regression test fits the documentation ones, and the gate fix has its planted test (it fails without the new rule).
