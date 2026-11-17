# Phase 24 / Day 376 — Levels and the Importer: Every Primitive Keeps Its Material

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 375; Phase 22 (the level format, the glTF importer, `scene::SpawnLevel`).

Close the Day 364 limitation: a spawned level gives each entity all the materials of its mesh.

## Tasks

- [ ] The level format's mesh-reference entry carries a material per slot (spike 6): the format's version is bumped, the fixtures are re-minted offline, and the loader reads only the current version (no load-time conversion, the Phase 22 rule); the entry validation (counts, ranges) is hostile-input safe and fuzzed with the existing level fuzz.
- [ ] `asset::ImportLevel` and `ImportGltf` write the material of every primitive into the level (a primitive without a material gets the default material resource); `asset_mint import-gltf --emit-level` likewise; two mints give identical bytes.
- [ ] `scene::SpawnLevel` and `level_spawn` give each spawned entity all the slots' materials; remove the Day 364 entry from `KNOWN-ISSUES.md` ("a level has one material per mesh").
- [ ] Tests: a generated `.glb` whose mesh has five primitives with five materials imports, spawns and shows five materials; a tree with two primitives (bark, leaves); a primitive without a material; an old-version level is refused with the status that says so; the mint determinism and the hostile-file tests.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the Windows PC and the Mac on Days 381-382, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` and the public headers carry `///` comments and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; `docs/guide/engine-library.md` (and the renderer or scene guide for a public header there) updated; the ABI snapshot regenerated when the table or a struct changed; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); only wrapped types, no `std`, no raw platform macros; every GPU-owning object keeps its CPU source and registers for the device reset.
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A glTF scene with multi-primitive meshes spawns as a level and every primitive draws with its own material.
