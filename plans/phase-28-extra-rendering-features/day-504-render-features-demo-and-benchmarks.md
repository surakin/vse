# Phase 28 / Day 504 — The Demo Scene and Benchmarks

**Phase:** [Phase 28 — Extra Rendering Features](../phase-28-extra-rendering-features.md)
**Depends on:** Days 478-501.

Show all four features together and measure them.

## Tasks

- [x] `game/render_features_demo`: a terrain of 2048x2048 samples with a cube-map sky (a minted gradient-and-sun cube made by the build), a forest of 200,000 instanced trees from an instance hierarchy (trunk, two branch levels, a leaf card cluster, with LODs and a distant stand-in) scattered on the terrain by ray cast, a few instanced rocks, a character walking on the height field and bodies rolling on it; the overlay shows the draw counts, the cull counters and the frame time.
- [x] Modes as the other demos: window, `--screenshot`, `--headless` (a hash of the physics state and of the visible-instance list at the final frame), `--sky procedural|cube`; options through `os::CommandLine`; settings for the rest.
- [x] `game/render_features_bench`: costs recorded in the guide pages: the cull of 1,000,000 instances at 1 to 16 threads, the terrain tile selection and draw count on 8192x8192, a hierarchy update of 100,000 nodes with 1% dirty, the sky pass cost, the memory per instance and per tree node; `bench_check`-style gate pattern as `script_bench_check` if the repo has one for benches.
- [x] Window runs with validation on Wayland and X11 for 1,000 frames (the `script_demo_window_*` pattern), and a screenshot test that compares against a committed reference image within a tolerance (the existing `model_viewer` screenshot method).
- [x] Tests: the headless hash equals at 1 to 16 threads; the demo runs 1,000 frames in a reset-heavy mode (the device reset every 100 frames) without a leak or validation error (asan and the existing reset soak pattern).
- [x] `docs/guide/render-features-demo.md` (reference style: how to run, the options).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --start-day` at the start; `scripts/test-phase.sh --affected` plain at the end, then the asan and tsan flavors (this day touches lifetime or concurrency code; tsan GPU tests are skipped by the existing environment switch); quote the final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 28); only wrapped types, no `std`, no raw platform macros (`build_defs.h`); code formatted by `scripts/format.sh <files>`.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels, `docgen lint` at zero, `cmake --build build --target docs` output committed, the guide page updated; published pages name no phases or days. No tutorials before the last day.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push (standing permission).

**Done:** The demo shows sky, terrain, instances and hierarchies at once, deterministic in its headless mode, with the measured costs recorded.

## Notes

- Done on the Linux machine, 2026-11-24. The limits and the one change from the plan (the sky cube is made when the program starts, not minted by the build) are in `KNOWN-ISSUES.md`, "Phase 28 Day 504".
- Files: `game/render_features_demo/{features_world,features_gpu}.{h,cpp}` (the world that needs no device, and the device side), `main.cpp` (window, `--screenshot`, `--headless`), `reference/forest.ppm`; `game/render_features_bench/main.cpp`; `docs/guide/render-features-demo.md`; module maps (`ARCHITECTURE.md`, `modules.txt`, `module_map.txt`).
- Pages touched: `docs/guide/render-features-demo.md` (new), `instancing.md` and `terrain.md` ("Cost"), `README.md` (the index); `ctest -L docs` passed (13 of 13). No public header changed.
- Measured costs (a Release build, Clang, 16 cores, RTX 3060): cull of 1,000,000 instances 1.225 / 0.658 / 0.524 / 0.316 / 0.206 ms at 1 / 2 / 4 / 8 / 16 threads (355,773 visible, the same at every count); tile selection on 8192 x 8192 0.0039 ms (126 tiles); hierarchy update, 1% of 100,000 nodes dirty, 0.184 ms at 1 thread (4,002 nodes recomposed); sky pass +0.072 ms (procedural) and +0.140 ms (cube) a frame at 1920 x 1080; memory 109.7 bytes an instance and 218.6 bytes a node.
- Runs: `scripts/test-phase.sh --start-day` at the start; plain `ran 4568 of 12233 tests (phases: all; flavors: plain; display: included; crash tests: excluded; soak: excluded) in 277 s` (failed: three X11 window tests and the audio snapshot cost test, both recorded; `container_use_check` failed on this day's FNV constants and was fixed); asan `ran 3750 of 12234 tests (...) in 195 s` (no failure); tsan `ran 3375 of 12235 tests (...) in 266 s` (only the recorded LuaJIT sandbox test). `render_features_bench_check` passed in `build-release`. The demo and the bench build clean with Clang and GCC, unity on and off.

- Written 2026-11-23 with the phase plan; the user's decisions are in `plans/phase-28-extra-rendering-features.md` ("Decisions").

## Post-implementation code review

`/code-review high` over the day's diff (inline, eight angles): 3 findings, 3 fixed.

1. **Fixed:** `FeaturesWorld::BuildMeshes` ignored the error strings of the mesh builders, so a failed mesh would show up later as "the forest refused a root". It now returns the first error and `Build` fails with it.
2. **Fixed:** `container_use_check` rejected the hand-written FNV-1a constants of the visible-list hash (`features_world.cpp`); it uses `kFnv1a64Basis` and `kFnv1a64Prime` (same values, same hashes).
3. **Fixed:** `--reset-every` with `--screenshot` aborted (an offscreen device has no device host to reset); the option is refused with a usage message and a test (`render_features_reset_needs_window`).

Declined, recorded in `KNOWN-ISSUES.md`: repeating `model_viewer`'s window and offscreen plumbing instead of a shared helper; the sky pass measured as a frame-time difference.
