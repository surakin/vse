# Phase 28 / Day 373 — Docs, Windows PC, Closing Full Run & Phase Review

**Phase:** [Phase 28 — 3D Text, Large 2D Text & Colored Glyphs](../phase-28-3d-text-and-color-glyphs.md)
**Depends on:** Days 358-367.

Close the phase: the Windows PC pass, the full suite, the benchmarks and the uncapped review.

## Tasks

- [ ] **Guide pass:** `docs/guide/text.md` and `docs/guide/image.md` read end to end; every tagged sample compiles and runs; `docgen lint` at zero; `cmake --build build --target docs`; `ctest -L docs`.
- [ ] **Windows PC:** pull, build `windows-msvc` (plain, asan) and `windows-clang-cl` in both unity modes; mint DXIL for `text_mesh` and any other Phase 28 shader and commit the `*.dxil.inc` files; run the Phase 28 tests and the demo on Direct3D 12 and Vulkan with validation; clear the "open until the other machine runs it" entries.
- [ ] **Benchmarks** against Day 358's baselines: atlas text must not regress beyond the noise band; the mesh paths' numbers go into the guide's cost table; anything beyond the band is fixed or filed with numbers.
- [ ] **Closing full run (CLAUDE.md):** `scripts/test-phase.sh --all --flavor all` on Linux and on Windows (plain, asan), `ctest -L docs`, `text3d_demo`, `ui_demo`, `renderer_demo`, `rhi_demo` 1000 frames with `--validation` on Wayland and X11; record each `ran X of Y tests (phases ...)` line here.
- [ ] **End-of-phase review (CLAUDE.md, uncapped):** `/code-review high --max-findings all` over the phase's whole diff (Days 358-373 from the commit before Day 358 to the working tree); verify, fix or decline each finding with a reason; record the count and outcomes under "Post-implementation code review".
- [ ] **Records:** `plans/phase-28-3d-text-and-color-glyphs.md` status complete, `PLAN.md` (roadmap row, log), `STATUS.md` (next: Phase 25 needs its day files before it starts), `KNOWN-ISSUES.md` sweep (the scaled-raster limit removed; limits of this phase added: no `COLR` v1, no `SVG` glyphs, no shaping; bitmap strikes are whatever FreeType and libpng read), `CURRENT_PHASE` stays 17 until Phase 17's first day starts; commit and push.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors if the day touched lifetime, memory or concurrency code; quote each run's final `ran X of Y tests (phases ...)` line in this file. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** the Windows and Linux full runs are recorded and pass, the benchmarks are within noise or filed, the uncapped review is resolved, and Phase 28 is marked complete.

## Notes

- The phase review also re-checks the Phase 16 rule: no `std` in any new file (the gate), and the new modules' rows in `ARCHITECTURE.md`.

## Post-implementation code review

(Filled in at the end of the day.)
