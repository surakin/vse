# Phase 24 / Day 374 — The Renderer Draws a Range of a Mesh

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Day 373.

The foundation of multi-material: one `GpuMesh` drawn as several objects, each over the index range of one material slot.

## Tasks

- [ ] `renderer::RenderObjectDesc` and `RenderObject` gain `first_index` and `index_count` (0 means the whole mesh); validation by value (a range outside the mesh or not a multiple of the topology's primitive size is refused and counted); the draw, the shadow and depth passes and the culling use the range.
- [ ] Per-range bounds for culling: the slot's own box when the mesh carries it (`MeshData` slot ranges give the triangles; compute the box once when the `GpuMesh` is made, deterministically, from the CPU copy it keeps), the mesh box otherwise; skinned objects keep `skin_bounds_scale`.
- [ ] The palette of a skinned mesh is uploaded once per frame per skeleton instance and shared by the objects of its slots (spike 1), not once per slot.
- [ ] Statistics: `objects_drawn` and the draw counts say whether they count objects or draws (stated in the guide); a range counts as one object.
- [ ] Tests: a mesh of two ranges draws each with its own material (golden image and analytic probes); a range outside the mesh is refused; culling drops a slot that is out of view and keeps the other; the device reset rebuilds the same picture (`tests/renderer/reset_rig.h`); the renderer guide section updated.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the Windows PC and the Mac on Days 381-382, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` and the public headers carry `///` comments and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/engine-library.md` (and the renderer or scene guide for a public header there) updated; the ABI snapshot regenerated when the table or a struct changed; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); only wrapped types, no `std`, no raw platform macros; every GPU-owning object keeps its CPU source and registers for the device reset.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** The renderer draws a range of a mesh with its own material, culls it by its own box and survives a reset.
