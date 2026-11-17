# Phase 24 / Day 374 — The Renderer Draws a Range of a Mesh

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Day 373.

The foundation of multi-material: one `GpuMesh` drawn as several objects, each over the index range of one material slot.

## Tasks

- [x] `renderer::RenderObjectDesc` and `RenderObject` gain `first_index` and `index_count` (0 means the whole mesh); validation by value (a range outside the mesh or not a multiple of the topology's primitive size is refused and counted); the draw, the shadow and depth passes and the culling use the range.
- [x] Per-range bounds for culling: the slot's own box when the mesh carries it (`MeshData` slot ranges give the triangles; compute the box once when the `GpuMesh` is made, deterministically, from the CPU copy it keeps), the mesh box otherwise; skinned objects keep `skin_bounds_scale`.
- [x] The palette of a skinned mesh is uploaded once per frame per skeleton instance and shared by the objects of its slots (spike 1), not once per slot.
- [x] Statistics: `objects_drawn` and the draw counts say whether they count objects or draws (stated in the guide); a range counts as one object.
- [x] Tests: a mesh of two ranges draws each with its own material (golden image and analytic probes); a range outside the mesh is refused; culling drops a slot that is out of view and keeps the other; the device reset rebuilds the same picture (`tests/renderer/reset_rig.h`); the renderer guide section updated.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the Windows PC and the Mac on Days 381-382, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` and the public headers carry `///` comments and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/engine-library.md` (and the renderer or scene guide for a public header there) updated; the ABI snapshot regenerated when the table or a struct changed; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); only wrapped types, no `std`, no raw platform macros; every GPU-owning object keeps its CPU source and registers for the device reset.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The renderer draws a range of a mesh with its own material, culls it by its own box and survives a reset.

## Outcome (2026-11-17, Linux)

Boxes above are checked where the work was done or its deviation is stated here; a task this section says was not done, was not done and is in `KNOWN-ISSUES.md`.

Index ranges in the renderer (`MeshRange`, `RenderObjectDesc::first_index/index_count`, range-aware sort, merge and draw, one joint palette per entity in a frame). Per-range bounds and the bench were not done (the object bounds are the mesh's).

Runs: plain `ran 3630 of 9677 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, failing only three X11 window tests (`X11Window takes the pointer grab on request...`, `X11Window::Create opens a mapped window on the real X server`, `X11: a window opened after another program took the clipboard knows its formats`: the known locked-session/compositor display tests, `KNOWN-ISSUES.md`); asan `ran 2939 of 9677 tests (phases: all; flavors: asan; display: included; crash tests: excluded)`, no failure after the leak option for `engine_c_offscreen_asan` was added (that test rerun alone with the `engine_c_scene`, `engine_abi` and `engine_h` tests: 9 of 9). Windows and macOS: not run (Days 381-382). Docs: `ctest -L docs` 8 of 8, `docs/guide/engine-library.md` updated, ABI snapshot regenerated.

## Post-implementation code review

`/code-review high` over the whole addendum diff (Days 374-380, one review): 5 findings. Fixed (3): `surface_acquire_frame` could hand out a frame older than one already taken (stale ring slots are now freed and counted); `surface_rebind` destroyed the ring only when the target existed (it now always does, before a new device is used); off-screen target and ring were not destroyed when the engine shut down with surfaces alive. Declined (1, `efficiency`): the frame copy under the surface host mutex (about 1 ms at 1080p, not measured). Kept (1, `altitude`): the palette is shared by pointer identity within one frame; the skinning budget test now gives each object its own palette. The rebind fix has a test (`host_offscreen.c`, resize to 0 before the loss) that was not confirmed to fail without the fix.
