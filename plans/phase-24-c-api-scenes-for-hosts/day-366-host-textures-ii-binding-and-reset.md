# Phase 24 / Day 366 — Host Textures II: Binding, Several Surfaces & Reset

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 362, 365.

A host texture appears on one entity's material slot and nowhere else, on every surface that shows the entity.

## Tasks

- [ ] Binding: a material-slot override (Day 362) may name a host texture; the entity draws it in place of that slot's base color; other entities sharing the material keep theirs; destroying the texture reverts the slot and counts it in the stats.
- [ ] Several surfaces: a texture is uploaded once per device that draws it (a surface's device, Day 358 decision), each device's copy follows the newest update; `surface_state` reports texture uploads per surface.
- [ ] Device reset and surface rebind: the textures re-upload from the kept pixels (the `DeviceResources` owner rules of CLAUDE.md: keep the CPU source, register at a tier, implement `Release`/`Recreate`, never cache a handle past a frame); a test resets the device through `tests/renderer/reset_rig.h` and compares `Device::Resources()` counts.
- [ ] Tests: three entities share one material, each gets a different pattern texture, pixels equal the goldens and the neighbors are untouched; a producer thread updates for 10 seconds under tsan while frames render; a resize mid-stream; a reset mid-stream; the same texture on two surfaces.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day; GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`**; `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still builds.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the other two no later than Day 372; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` carries its comment convention (set on Day 346) for every addition and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated; the ABI snapshot (`abi/engine_api.snapshot`) is regenerated and the version rule of the spec is followed.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); a new C test host gets its `std_usage_check` allow-list entry.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** live textures show on single entities across several surfaces and survive a device reset under tsan.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
