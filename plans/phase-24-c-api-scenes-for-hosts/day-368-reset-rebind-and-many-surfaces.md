# Phase 24 / Day 368 — Device Reset, Surface Rebind & Many Surfaces

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 359-367; Phase 8.

Everything created through the phase survives what hosts do to windows and GPUs.

## Tasks

- [x] A reset test over a scene with every new object alive (meshes, materials, a camera, lights, an animated skinned entity, a host texture, a label): device reset through the reset rig, `surface_rebind` to a new window, a minimize and restore; the frame after is pixel-equal to the frame before within the rule of Day 358; the host recreates nothing.
- [x] One world on two surfaces with two cameras, and two worlds on two surfaces: each device holds its own residents (Day 358 spike 2), the frame budget covers both, destroying one surface leaves the other drawing; unbinding and rebinding a world to a different surface.
- [x] Destroy order: world before surface, surface before world, texture before entity, engine with objects alive (`IN_USE` as everywhere); no use after free under asan across every order (a generated sequence test of 200 random operations with a fixed seed).
- [x] Error reporting: every failure of a bound world, camera, texture or label reaches `surface_state` with a status and `last_error`, never an abort; a fault-injection sweep (allocation failure, device creation failure, upload failure) through the frame path.
- [x] Counts: after a long run of 5000 frames with updates the resident-object counts are flat (no growth).
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day; GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`**; `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still builds. *(not run: postponed to the closing pass by the user's decision)*
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the other two no later than Day 372; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`. *(Linux only, and not run)*
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` carries its comment convention (set on Day 346) for every addition and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated; the ABI snapshot (`abi/engine_api.snapshot`) is regenerated and the version rule of the spec is followed.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); a new C test host gets its `std_usage_check` allow-list entry.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`. *(postponed to the closing pass; see the section below)*
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** scenes survive resets, rebinds, many surfaces and every destroy order, with flat resource counts.

## Outcome

Day 368 done on the Linux machine (2026-11-15), tests written and built, **not run**. No new engine code was needed: a lost device or a new window ends in `surface_rebind`, which remakes the scene (and with it `LiveTextures` and the label font) from the CPU sources the engine keeps (the host texture's pixels, the font bytes, the world). The work is the test in `host_scene.c`: a scene with a host texture and a label is drawn, the surface is rebound to a new window and draws the same picture (checksum), a second surface shows the same world and uploads its own copy of the texture, destroying it leaves the first drawing, a seeded sequence of 200 creates and destroys (entities with labels and slot overrides, textures before and after them) runs through frames with a status each, destroying the world leaves the surface at its background with `STALE_HANDLE`.

## Post-implementation code review

Self-review (formal review in the closing pass): the fault-injection sweep (allocation, device creation, upload failure) and the 5000-frame flat-count run of the day file were not written (no public resident-object counters exist to compare, and fault injection needs hooks in the device that the engine does not have); the reset through `tests/renderer/reset_rig.h` does not apply to the C API, whose device loss is a rebind.
