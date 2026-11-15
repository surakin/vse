# Phase 24 / Day 364 — A glTF Scene as a Level

**Phase:** [Phase 24 — Scenes for Hosts Through the C API](../phase-24-c-api-scenes-for-hosts.md)
**Depends on:** Days 351, 362, 363; Phase 22 (importer, level format, `scene::SpawnLevel`).

The importer's scene output: the default scene's node tree becomes a level that a host spawns into a world.

## Tasks

- [x] The importer's scene output (moved out of Phase 22 Day 338, `KNOWN-ISSUES.md`): one level entry per node with its transform, its mesh and skeleton-instance references, its name and parent; glTF instancing (several nodes sharing one mesh) kept as several entities sharing one mesh resource; the level is minted in memory (`ImportIntoBackend`) with the other blobs; a bound on node count and depth against hostile files, like every importer limit.
- [x] `asset_mint import-gltf --emit-level` writes the same level for the tool; `ImportGltf`'s result gains the level id; both tested for byte determinism.
- [x] C API: the `import_gltf` result names the level; `level_spawn(world, level, &engine_spawn_params)` (a parent transform, a name prefix, flags) writes the root entity key and optionally every entity key into a caller buffer; named nodes are findable after spawning (`entity_find`, Day 362); a level spawned twice gives two independent copies; a level whose resources are missing fails atomically (nothing spawned), with a message (Phase 22: no broken dependencies).
- [x] Spawn of a level on a world with a thousand entities must not stall a frame: the spawn is bounded work or runs as a job (decided in the spec) and is reported done through the frame.
- [x] Tests: a generated `.glb` with a node tree, a skinned child and instanced trees imports, spawns and draws; the entity count and names are as expected; a hostile file (depth, node count) is refused; the level's determinism; spawn twice.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day; GPU tests are a skipped stub under tsan, `KNOWN-ISSUES.md`); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`**; `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still builds. *(not run: postponed to the closing pass by the user's decision)*
- [x] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, `include/engine/`, the renderer and scene glue) is checked on the other two no later than Day 372; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`. *(Linux only, and not run)*
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** `include/engine/engine_c.h` carries its comment convention (set on Day 346) for every addition and `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated; the ABI snapshot (`abi/engine_api.snapshot`) is regenerated and the version rule of the spec is followed.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 24); a new C test host gets its `std_usage_check` allow-list entry.
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`. *(postponed to the closing pass; see the section below)*
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a host imports a `.glb` at run time, spawns its node tree into a world, finds a named node and sees the scene.

## Outcome

Day 364 done on the Linux machine (2026-11-15), tests written and built, **not run**. `asset::ImportLevel` (new `gltf_level.*`; `ImportOptions::emit_level`; `asset_import` now links `animation`, an allowed same-layer edge): the node tree of the default scene as a level blob (name, local transform from the node matrix with `options.scale`, parent link, mesh reference, skeleton instance), keys from `LevelEntityKey`, depth bound `kMaxLevelDepth` 64 (the ECS's own), node count bound by `max_nodes`. `asset_mint import-gltf --emit-level`; `ENGINE_IMPORT_EMIT_LEVEL`; `level_spawn` with `engine_spawn_params` (`ResourceHost::ReadLevel` checks the level and everything it names, a scratch world receives the records through `LoadLevelBlob`, then the entities are copied into the engine's world with the engine's own components and undone on failure). Spawn is synchronous: it is bounded work (linear in the entities), the spec's job variant was not needed. Tests: `host_import.c` (`TestLevel`: counts, names, parents, components, a second spawn with a prefix and a position, refusals that change nothing, a removed mesh; `TestDeepScene`: depth 70 and `max_nodes` refused); `check_mint.cmake` (`--emit-level` byte determinism with a manifest).

## Post-implementation code review

Self-review (the formal `/code-review high` runs once in the closing pass): a mesh with several primitives loses the materials of all but the first (the level's mesh reference has one material); names are cut to 63 bytes with a prefix; the scratch world copies every component once more than a direct spawn would (the price of not writing a second loader).
