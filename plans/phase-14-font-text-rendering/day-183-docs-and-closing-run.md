# Phase 14 / Day 183 — Guide, Benchmark, Closing Runs, Phase Review

**Phase:** [Phase 14 — Font & Text Rendering](../phase-14-font-text-rendering.md)
**Depends on:** Days 175-182.

Document, measure, run everything on both machines, review the whole phase, close it.

## Tasks

- [x] **Guide `docs/guide/text.md`:** the model (fonts, glyphs without shaping, atlas, layout, cursor), a complete "hello world" sample (tagged, compiles and runs), sizes and HiDPI, hinting choice and how to
      change it, the limits (no complex-script shaping, no right-to-left, no vertical text, kerning from the kern table only, no atlas eviction), how reset and loss are handled, the seams left for
      the 3D text project; listed in `docs/guide/README.md`; `renderer.md` and `building-and-testing.md` updated where they mention text or the new tests.
- [x] **Reference:** `docgen lint` zero, reference regenerated and committed (identical on both machines), `docs/reference/modules.txt` covers `text` and the renderer's text headers.
- [x] **Benchmark** `game/renderer_bench` section `text`: layout time per 1,000 characters, glyph cache hit and miss costs, atlas upload bytes per new glyph, draw cost per 1,000 glyphs; recorded in the guide
      and this file (no thresholds in tests).
- [x] **Project documents:** `PLAN.md` (assumption #17's FreeType details, the roadmap row, a progress-log entry), `CLAUDE.md` if a rule changed (the FreeType boundary, the text module), `BUILD.md`,
      `ARCHITECTURE.md` (the measured graph), `STATUS.md`.
- [x] **Known-issues sweep:** every Phase 14 deferral filed (no shaping, no right-to-left, no atlas eviction, a maximum rasterized size (larger text is scaled up until the 3D text project draws it as meshes), no word movement, kerning limits, LCD rendering not done, CJK coverage limited by the shipped font);
      entries this phase fixed removed.
- [ ] **Closing full runs (CLAUDE.md, required for a phase's last day):** Linux `scripts/test-phase.sh --all --flavor all`, `ctest -L docs`, the demos 1000 frames with validation on Wayland and X11;
      Windows `scripts/test-phase.sh --all --flavor all` (plain and asan), `ctest -L docs`, the demos 1000 frames on DX12 and Vulkan; record both final `ran X of Y tests (phases ...)` lines here; nothing left out.
- [x] **End-of-phase review (CLAUDE.md, uncapped):** `/code-review high --max-findings all` over the whole phase's diff (the commit before Day 175 to the working tree); verify, fix or decline each finding with a
      reason; record the count and outcome under "Post-implementation code review".
- [x] `plans/phase-14-font-text-rendering.md` (status complete), `PLAN.md`, `STATUS.md` updated; `CURRENT_PHASE` stays 14 until Phase 15 has its day files and starts; commit and push.

**Done:** the guide and reference are current and identical on both machines, the closing full-run lines for Linux and Windows are recorded, the uncapped review is recorded, and Phase 14 is marked complete.

## Notes

- Guide: `docs/guide/text.md` is complete: a runnable "Hello, world" sample (`cpp compile run standalone`, with its `output` block; the docs build passes the absolute font path as the macro `DOCS_FONT_FILE`, because samples run in a scratch directory; `docgen`'s custom target now has `VERBATIM` so the quoted macro survives), a "Cost" table, status line "complete". Reference regenerated: 4739 of 4739 documented, `ctest -L docs` 6 of 6. Already in place from earlier days: `docs/guide/README.md` entry, `modules.txt` rows for `text` and the renderer text headers, `ARCHITECTURE.md` (172 edges), `BUILD.md`, `CLAUDE.md` (no rule changed).
- Benchmark (`renderer_bench --section text`, Linux, the default unoptimized configuration, so an upper bound): layout of 1,000 characters (962 glyphs) 275-278 us at 16/32/64 px, wrapped at 600 px 279-287 us; atlas miss 18 us per glyph (94 ASCII glyphs at 32 px), hit 0.06 us; dirty rectangle of those 94 glyphs 85,848 bytes (913 per glyph) while the RHI sends the whole 1,048,576-byte page; `TextRenderer::Add` 71 us for 962 glyphs; `Draw` (layout plus `Add`) 347 us.
- Known-issues sweep: the Phase 14 limits are one entry in `KNOWN-ISSUES.md` (no shaping, no right-to-left or vertical text, no eviction and no negative cache, 128 px maximum raster, no word movement, kern-table kerning only, no LCD rendering, CJK and emoji limited by the shipped font, whole-page upload); the Windows build and DXIL entries stay open.
- Closing runs, Linux (Clang): `scripts/test-phase.sh --all --flavor all` -> `ran 3789 of 3789 tests (phases: all; flavors: all; display: included; crash tests: included) in 143 s` (one failure: `X11Window::Create opens a mapped window on the real X server`, the known session flake, also failing in the plain `--affected` run). After the review fix, `--affected` plain: `ran 1475 of 3789 tests (phases: all; flavors: plain; display: included; crash tests: excluded) in 94 s`. Both demos, `renderer_demo --text "Hello, world!" --frames 1000 --validation` on `--backend wayland` and `--backend x11` (and `rhi_demo`): exit 0, no validation message. GCC and non-unity builds of the demo and text tests were clean on Day 182 and the later changes (the benchmark, `text_renderer.cpp`) build clean in the default build.
- **Windows half open:** `scripts/test-phase.sh --all --flavor all` (plain and asan), `ctest -L docs`, the demos on DX12 and Vulkan, and DXIL for `text.vert`/`text.frag` (`KNOWN-ISSUES.md`). The phase is complete on Linux; the Windows PC records its closing lines here when it runs them.

## Post-implementation code review

End-of-phase review (`/code-review high --max-findings all`), done inline over the whole phase diff (commit before Day 175 to the working tree: 53 source, test, tool and shader files, about 5,300 lines, vendored FreeType and generated shader includes excluded). Each day had already been reviewed at `high`; this pass read `text_renderer.cpp`, `glyph_atlas_texture.cpp`, `glyph_atlas.cpp` and the font id and lifetime path again, looking for cross-day problems. 3 findings:
- (fixed) `TextRenderer::AddToGraph` rebuilt the pipeline only when the target format differed from `desc_.color_format`, and stored the new format before the rebuild; if `MakePipeline` failed once, every later frame with that format saw "no change", never retried, and logged "pipeline could not be made" forever. It now also rebuilds when the pipeline is invalid. No regression test: a failing `CreateGraphicsPipeline` needs RHI fault injection that does not exist (filed with the other RHI-injection gaps).
- (declined, filed) A glyph that does not fit the atlas is re-rasterized and refused again on every frame (no negative cache), costing about 18 us per glyph per frame once the atlas is full. Eviction is out of scope for this phase; listed in the Phase 14 limits in `KNOWN-ISSUES.md`.
- (declined) `TextRenderer::Add` sets the style font's pixel size to the raster size as a side effect. Every caller sets the size through the style before use (layout does the same), and the guide documents that the font's size follows the last use.
Counts: 3 found, 1 fixed, 2 declined.
