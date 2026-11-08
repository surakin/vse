# Phase 14 / Day 183 — Guide, Benchmark, Closing Runs, Phase Review

**Phase:** [Phase 14 — Font & Text Rendering](../phase-14-font-text-rendering.md)
**Depends on:** Days 175-182.

Document, measure, run everything on both machines, review the whole phase, close it.

## Tasks

- [ ] **Guide `docs/guide/text.md`:** the model (fonts, glyphs without shaping, atlas, layout, cursor), a complete "hello world" sample (tagged, compiles and runs), sizes and HiDPI, hinting choice and how to
      change it, the limits (no complex-script shaping, no right-to-left, no vertical text, kerning from the kern table only, no atlas eviction), how reset and loss are handled, the seams left for
      Phase 16; listed in `docs/guide/README.md`; `renderer.md` and `building-and-testing.md` updated where they mention text or the new tests.
- [ ] **Reference:** `docgen lint` zero, reference regenerated and committed (identical on both machines), `docs/reference/modules.txt` covers `text` and the renderer's text headers.
- [ ] **Benchmark** `game/renderer_bench` section `text`: layout time per 1,000 characters, glyph cache hit and miss costs, atlas upload bytes per new glyph, draw cost per 1,000 glyphs; recorded in the guide
      and this file (no thresholds in tests).
- [ ] **Project documents:** `PLAN.md` (assumption #17's FreeType details, the roadmap row, a progress-log entry), `CLAUDE.md` if a rule changed (the FreeType boundary, the text module), `BUILD.md`,
      `ARCHITECTURE.md` (the measured graph), `STATUS.md`.
- [ ] **Known-issues sweep:** every Phase 14 deferral filed (no shaping, no right-to-left, no atlas eviction, no word movement, kerning limits, LCD rendering not done, CJK coverage limited by the shipped font);
      entries this phase fixed removed.
- [ ] **Closing full runs (CLAUDE.md, required for a phase's last day):** Linux `scripts/test-phase.sh --all --flavor all`, `ctest -L docs`, the demos 1000 frames with validation on Wayland and X11;
      Windows `scripts/test-phase.sh --all --flavor all` (plain and asan), `ctest -L docs`, the demos 1000 frames on DX12 and Vulkan; record both final `ran X of Y tests (phases ...)` lines here; nothing left out.
- [ ] **End-of-phase review (CLAUDE.md, uncapped):** `/code-review high --max-findings all` over the whole phase's diff (the commit before Day 175 to the working tree); verify, fix or decline each finding with a
      reason; record the count and outcome under "Post-implementation code review".
- [ ] `plans/phase-14-font-text-rendering.md` (status complete), `PLAN.md`, `STATUS.md` updated; `CURRENT_PHASE` stays 14 until Phase 15 has its day files and starts; commit and push.

**Done:** the guide and reference are current and identical on both machines, the closing full-run lines for Linux and Windows are recorded, the uncapped review is recorded, and Phase 14 is marked complete.

## Notes

(filled in as the day goes)

## Post-implementation code review

(filled in at the end of the day)
