# Phase 28 / Day 504 — The Demo Scene and Benchmarks

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 478-501.

Show all four features together and measure them.

## Tasks

- [ ] `game/render_features_demo`: a terrain of 2048x2048 samples with a cube-map sky (a minted gradient-and-sun cube made by the build), a forest of 200,000 instanced trees from an instance hierarchy (trunk, two branch levels, a leaf card cluster, with LODs and a distant stand-in) scattered on the terrain by ray cast, a few instanced rocks, a character walking on the height field and bodies rolling on it; the overlay shows the draw counts, the cull counters and the frame time.
- [ ] Modes as the other demos: window, `--screenshot`, `--headless` (a hash of the physics state and of the visible-instance list at the final frame), `--sky procedural|cube`; options through `os::CommandLine`; settings for the rest.
- [ ] `game/render_features_bench`: costs recorded in the guide pages: the cull of 1,000,000 instances at 1 to 16 threads, the terrain tile selection and draw count on 8192x8192, a hierarchy update of 100,000 nodes with 1% dirty, the sky pass cost, the memory per instance and per tree node; `bench_check`-style gate pattern as `script_bench_check` if the repo has one for benches.
- [ ] Window runs with validation on Wayland and X11 for 1,000 frames (the `script_demo_window_*` pattern), and a screenshot test that compares against a committed reference image within a tolerance (the existing `model_viewer` screenshot method).
- [ ] Tests: the headless hash equals at 1 to 16 threads; the demo runs 1,000 frames in a reset-heavy mode (the device reset every 100 frames) without a leak or validation error (asan and the existing reset soak pattern).
- [ ] `docs/guide/render-features-demo.md` (reference style: how to run, the options).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** The demo shows sky, terrain, instances and hierarchies at once, deterministic in its headless mode, with the measured costs recorded.

## Notes

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

(To be filled in when the day is done.)
