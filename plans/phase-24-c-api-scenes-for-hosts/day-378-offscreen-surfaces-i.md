# Phase 24 / Day 378 — Off-Screen Surfaces I: A Surface with No Window

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 373, 377; Phase 23 (surfaces, `surface_input`), Phase 24 Days 359-360.

Create, bind, resize and destroy a surface that has no native window.

## Tasks

- [ ] `ENGINE_SURFACE_OFFSCREEN` (and a capability bit in the engine's capability query): `surface_create` takes only `width`, `height` (pixels) and `scale` for this kind; the surface owns a colour target (and the depth and G-buffer targets the frame graph needs) on the device chosen as in spike 4; no swapchain.
- [ ] Everything that takes a surface takes this one: `surface_set_world`, `surface_set_camera`, `surface_set_background`, `surface_resized` (reallocates the targets; a size of 0 hides it and frees what it can), `surface_state`, `surface_pixel_ray`, `surface_project`, `surface_input`, `surface_destroy`.
- [ ] The frame: `engine_frame` renders every off-screen surface that has a bound world and a non-zero size (spike 3 decides dirty or visible rules); counters in `surface_state`: frames rendered.
- [ ] Input: `surface_input` works unchanged (the host translates its own pointer and key events into surface pixels); pointer lock goes through the existing host callback `set_pointer_lock`, which the host implements.
- [ ] Tests without any window system: a C program creates an off-screen surface, binds the Day 359 scene, runs frames and reads the picture back with the existing `surface_read_pixels` (the per-frame path is Day 379); resize, hide, destroy while frames are in flight (asan).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the Windows PC and the Mac on Days 381-382, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` and the public headers carry `///` comments and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/engine-library.md` (and the renderer or scene guide for a public header there) updated; the ABI snapshot regenerated when the table or a struct changed; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); only wrapped types, no `std`, no raw platform macros; every GPU-owning object keeps its CPU source and registers for the device reset.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A host with no window of its own creates an off-screen surface, binds a world to it and renders frames.
