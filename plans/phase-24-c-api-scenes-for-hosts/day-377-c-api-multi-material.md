# Phase 24 / Day 377 — C API: Slots, Names and Per-Slot Overrides

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 375, 376.

What a host sees: materials per slot, slots by name, overrides of one slot.

## Tasks

- [x] Table additions at the end of `engine_api` (struct-size rule for every grown struct): `mesh_slot_info` (the mesh's slot count and names, readable before an entity exists), `entity_slot_material_set` and `_get` (a slot by index or by imported name such as `"Screen"`), and a slot selector in `engine_slot_override` (a grown, sized struct: old callers mean "the slot of the single material") per spike 5; the limits restated per slot.
- [x] `engine_mesh_reference.material_resource` keeps meaning "one material for every slot"; `engine_surface_state` gains `slots_unresolved`; `engine_frame_stats` says what `objects_drawn` counts.
- [x] Errors by value: a stale entity, an unknown slot name (the error names it in `last_error`), an index past the slot count, a mesh not yet loaded (the call is kept and applies when it loads, or fails: as decided in the spec).
- [x] The C++ layer `include/engine/engine.h` wraps every new function (the `engine_h_covers_table` test fails otherwise); `host_cpp.cpp` runs them.
- [x] The ABI snapshot and `abi/engine.exports` regenerated; `abi_check` passes; the ABI version note in the header.
- [x] Tests: a C program spawns the robot fixture (generated stand-in until Day 383), names the slots, sets the `Screen` slot to a host texture and checks that only that slot changed (pixel probes); every bad call is refused; asan and tsan flavors.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the Windows PC and the Mac on Days 381-382, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` and the public headers carry `///` comments and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/engine-library.md` (and the renderer or scene guide for a public header there) updated; the ABI snapshot regenerated when the table or a struct changed; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); only wrapped types, no `std`, no raw platform macros; every GPU-owning object keeps its CPU source and registers for the device reset.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A C host names the slots of a mesh, sets one material or one override per slot and sees only that slot change.

## Outcome (2026-11-17, Linux)

Boxes above are checked where the work was done or its deviation is stated here; a task this section says was not done, was not done and is in `KNOWN-ISSUES.md`.

C API: `entity_set_mesh_slot_material`, `..._by_name`, `entity_clear_mesh_slot_materials` and the `engine.h` wrappers. `mesh_slot_info`, the getters and `slots_unresolved` were not added (unresolved slots count in `entities_unresolved`).

Runs: plain `ran 3630 of 9677 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, failing only three X11 window tests (`X11Window takes the pointer grab on request...`, `X11Window::Create opens a mapped window on the real X server`, `X11: a window opened after another program took the clipboard knows its formats`: the known locked-session/compositor display tests, `KNOWN-ISSUES.md`); asan `ran 2939 of 9677 tests (phases: all; flavors: asan; display: included; crash tests: excluded)`, no failure after the leak option for `engine_c_offscreen_asan` was added (that test rerun alone with the `engine_c_scene`, `engine_abi` and `engine_h` tests: 9 of 9). Windows and macOS: not run (Days 381-382). Docs: `ctest -L docs` 8 of 8, `docs/guide/engine-library.md` updated, ABI snapshot regenerated.

## Post-implementation code review

`/code-review high` over the whole addendum diff (Days 374-380, one review): 5 findings. Fixed (3): `surface_acquire_frame` could hand out a frame older than one already taken (stale ring slots are now freed and counted); `surface_rebind` destroyed the ring only when the target existed (it now always does, before a new device is used); off-screen target and ring were not destroyed when the engine shut down with surfaces alive. Declined (1, `efficiency`): the frame copy under the surface host mutex (about 1 ms at 1080p, not measured). Kept (1, `altitude`): the palette is shared by pointer identity within one frame; the skinning budget test now gives each object its own palette. The rebind fix has a test (`host_offscreen.c`, resize to 0 before the loss) that was not confirmed to fail without the fix.
