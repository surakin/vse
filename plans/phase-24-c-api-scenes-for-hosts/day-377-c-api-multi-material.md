# Phase 24 / Day 377 — C API: Slots, Names and Per-Slot Overrides

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 375, 376.

What a host sees: materials per slot, slots by name, overrides of one slot.

## Tasks

- [ ] Table additions at the end of `engine_api` (struct-size rule for every grown struct): `mesh_slot_info` (the mesh's slot count and names, readable before an entity exists), `entity_slot_material_set` and `_get` (a slot by index or by imported name such as `"Screen"`), and a slot selector in `engine_slot_override` (a grown, sized struct: old callers mean "the slot of the single material") per spike 5; the limits restated per slot.
- [ ] `engine_mesh_reference.material_resource` keeps meaning "one material for every slot"; `engine_surface_state` gains `slots_unresolved`; `engine_frame_stats` says what `objects_drawn` counts.
- [ ] Errors by value: a stale entity, an unknown slot name (the error names it in `last_error`), an index past the slot count, a mesh not yet loaded (the call is kept and applies when it loads, or fails: as decided in the spec).
- [ ] The C++ layer `include/engine/engine.h` wraps every new function (the `engine_h_covers_table` test fails otherwise); `host_cpp.cpp` runs them.
- [ ] The ABI snapshot and `abi/engine.exports` regenerated; `abi_check` passes; the ABI version note in the header.
- [ ] Tests: a C program spawns the robot fixture (generated stand-in until Day 383), names the slots, sets the `Screen` slot to a host texture and checks that only that slot changed (pixel probes); every bad call is refused; asan and tsan flavors.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the Windows PC and the Mac on Days 381-382, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` and the public headers carry `///` comments and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/engine-library.md` (and the renderer or scene guide for a public header there) updated; the ABI snapshot regenerated when the table or a struct changed; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); only wrapped types, no `std`, no raw platform macros; every GPU-owning object keeps its CPU source and registers for the device reset.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A C host names the slots of a mesh, sets one material or one override per slot and sees only that slot change.
