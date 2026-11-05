# Phase 4 / Day 83 — Hardening, Sanitizers & Performance

**Phase:** [Phase 4 — Renderer Core](../phase-04-renderer-core.md)
**Depends on:** Day 82.

Make the renderer robust and know what it costs.

## Tasks

- [ ] Sanitizers: `renderer_tests_asan` green with GPU tests, `renderer_tests_tsan` green for the null-device parallel recording and graph tests, leak counters (renderer-level and `GetLeakCounts`) at zero after every test; suppressions only with a documented reason.
- [ ] Failure injection: device loss at each stage of a frame (before the G-buffer, mid-lighting, at present), an allocation failure creating a transient, a material with a missing texture, a pipeline that fails to build - each ends with a logged error, a defined status and a destroyable renderer.
- [ ] Review every `ENGINE_ASSERT` and log site in `engine/renderer`, classify each (programming error vs environment) as for the RHI, and document with `Aborts:`/`Fails:`.
- [ ] Benchmarks (`game/renderer_bench`, recorded in the day file, never asserted): graph build+compile time vs pass count, CPU frame time for 1k/10k/50k objects (single thread vs jobs), draws and bind changes after sorting, light volume cost per light, transient pool hit rate; compare per-draw cost with the Phase 3 `rhi_bench` figure (0.5-0.6 us) and explain the difference.
- [ ] Profile the 10k-object frame with `perf`, record the top hotspots as notes for later phases, and fix only defects.
- [ ] Run the whole suite plain/ASan/TSan and record the renderer test set's timing against a documented budget.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** The failure-behavior table and performance notes are in the guide. `docgen lint` stays at zero findings, `cmake --build build --target docs` output is committed, `ctest -L docs` passes.
- [ ] **End-of-day review (CLAUDE.md):** run `/code-review high` on the day's diff, fix/decline each finding with a reason, record it under a "Post-implementation code review" section here, and mention it in `STATUS.md`.
- [ ] Update `STATUS.md`; commit (code, tests, regenerated `docs/reference/` and the guide pages together).

**Done:** The renderer passes all sanitizer and failure-injection runs, its costs are measured and explained, and nothing leaks over long runs.
