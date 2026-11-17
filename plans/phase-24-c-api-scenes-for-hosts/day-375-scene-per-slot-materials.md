# Phase 24 / Day 375 — The Scene Bridge: One Material per Slot

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Day 374.

`scene::MeshRenderer` and `RenderSync` draw every slot of a mesh.

## Tasks

- [ ] `MeshRenderer` carries a material per slot (an inline array up to the mesh format's slot limit, `asset::kMaxMeshSlots`, with a count) beside the existing single `material` (which means "this material for every slot" for meshes with one slot and for old callers); the shape decided in the spec.
- [ ] `RenderSync` emits one render object per slot of the mesh (ranges of Day 374) with the entity's transform, flags, `Hidden` and palette; a slot with no material uses the mesh's imported default or the surface's default grey, and is counted when it cannot be resolved.
- [ ] Per-slot overrides: the Day 362/365/366 mechanism (a private copy of a material with one slot value replaced) works per mesh slot: an entity can override `albedo_tex` of its slot named `Screen` without touching its other slots or other entities.
- [ ] Skinned meshes: every slot draws skinned with the same skeleton instance; the animation system and the joint attachment are unchanged.
- [ ] Cost: an entity of a one-slot mesh costs what it did (a test counts the objects and the sync time on the Day 359 scene); a thousand instances of a two-slot mesh stay inside the budget of spike 1.
- [ ] Tests: a mesh of five slots draws five materials; a slot override changes one slot on one entity; hidden hides all slots; a destroyed material resolves to the default and is counted; reset test.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the Windows PC and the Mac on Days 381-382, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` and the public headers carry `///` comments and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/engine-library.md` (and the renderer or scene guide for a public header there) updated; the ABI snapshot regenerated when the table or a struct changed; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); only wrapped types, no `std`, no raw platform macros; every GPU-owning object keeps its CPU source and registers for the device reset.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** An entity draws every slot of its mesh with its own material and a per-slot override changes one slot of one entity.
