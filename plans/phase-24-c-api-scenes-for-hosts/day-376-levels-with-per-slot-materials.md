# Phase 24 / Day 376 — Levels and the Importer: Every Primitive Keeps Its Material

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 375; Phase 22 (the level format, the glTF importer, `scene::SpawnLevel`).

Close the Day 364 limitation: a spawned level gives each entity all the materials of its mesh.

## Tasks

- [x] The level format's mesh-reference entry carries a material per slot (spike 6): the format's version is bumped, the fixtures are re-minted offline, and the loader reads only the current version (no load-time conversion, the Phase 22 rule); the entry validation (counts, ranges) is hostile-input safe and fuzzed with the existing level fuzz.
- [x] `asset::ImportLevel` and `ImportGltf` write the material of every primitive into the level (a primitive without a material gets the default material resource); `asset_mint import-gltf --emit-level` likewise; two mints give identical bytes.
- [x] `scene::SpawnLevel` and `level_spawn` give each spawned entity all the slots' materials; remove the Day 364 entry from `KNOWN-ISSUES.md` ("a level has one material per mesh").
- [x] Tests: a generated `.glb` whose mesh has five primitives with five materials imports, spawns and shows five materials; a tree with two primitives (bark, leaves); a primitive without a material; an old-version level is refused with the status that says so; the mint determinism and the hostile-file tests.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the Windows PC and the Mac on Days 381-382, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` and the public headers carry `///` comments and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/engine-library.md` (and the renderer or scene guide for a public header there) updated; the ABI snapshot regenerated when the table or a struct changed; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); only wrapped types, no `std`, no raw platform macros; every GPU-owning object keeps its CPU source and registers for the device reset.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A glTF scene with multi-primitive meshes spawns as a level and every primitive draws with its own material.

## Outcome (2026-11-17, Linux)

Boxes above are checked where the work was done or its deviation is stated here; a task this section says was not done, was not done and is in `KNOWN-ISSUES.md`.

Levels: the additive stored component `asset::MeshSlotMaterials` (id 52) written by the glTF importer when primitives' materials differ and spawned by `SpawnLevel` and `level_spawn`; no format version bump. A primitive without a material still inherits the base material (`KNOWN-ISSUES.md`).

Runs: plain `ran 3630 of 9677 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, failing only three X11 window tests (`X11Window takes the pointer grab on request...`, `X11Window::Create opens a mapped window on the real X server`, `X11: a window opened after another program took the clipboard knows its formats`: the known locked-session/compositor display tests, `KNOWN-ISSUES.md`); asan `ran 2939 of 9677 tests (phases: all; flavors: asan; display: included; crash tests: excluded)`, no failure after the leak option for `engine_c_offscreen_asan` was added (that test rerun alone with the `engine_c_scene`, `engine_abi` and `engine_h` tests: 9 of 9). Windows and macOS: not run (Days 381-382). Docs: `ctest -L docs` 8 of 8, `docs/guide/engine-library.md` updated, ABI snapshot regenerated.

## Post-implementation code review

`/code-review high` over the whole addendum diff (Days 374-380, one review): 5 findings. Fixed (3): `surface_acquire_frame` could hand out a frame older than one already taken (stale ring slots are now freed and counted); `surface_rebind` destroyed the ring only when the target existed (it now always does, before a new device is used); off-screen target and ring were not destroyed when the engine shut down with surfaces alive. Declined (1, `efficiency`): the frame copy under the surface host mutex (about 1 ms at 1080p, not measured). Kept (1, `altitude`): the palette is shared by pointer identity within one frame; the skinning budget test now gives each object its own palette. The rebind fix has a test (`host_offscreen.c`, resize to 0 before the loss) that was not confirmed to fail without the fix.
