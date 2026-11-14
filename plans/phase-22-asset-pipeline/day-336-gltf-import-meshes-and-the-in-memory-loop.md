# Phase 22 / Day 336 — glTF Import II: Meshes, and the First In-Memory Import

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Days 315 (`HostMemoryBackend`), 319 (mesh), 328 (the resource manager), 335 (the importer library).

Imports glTF meshes into mesh resources through the library of Day 335 and closes the first loop of the the host use: **`.glb` bytes in, mesh blobs in a `HostMemoryBackend`, the resource manager loads them.**

## Tasks

- [ ] **Meshes:** each glTF mesh becomes one engine mesh resource; each primitive becomes a material slot (named after the material, or `primitive<N>`) with its own vertex range appended to the mesh's vertex pool and its index range; modes `TRIANGLES`, `TRIANGLE_STRIP` and `TRIANGLE_FAN` are converted to a list, other modes are refused with the mesh and primitive named; indices are widened or narrowed as the mesh rule requires.
- [ ] **Missing attributes, deterministically:** no normals: area-weighted smooth normals (flat when `--flat-normals`); no tangents: per-triangle UV-derivative tangents accumulated per vertex and orthonormalized, bitangent sign from the UV handedness (documented, deterministic, **not** MikkTSpace; filed in `KNOWN-ISSUES.md`); no UVs: zeros with a counted warning; vertex colors and `TEXCOORD_1` and up are dropped with a counted warning.
- [ ] **Engine conventions:** the importer converts glTF's right-handed, Y-up, metre units to the engine's convention (decided on this day from `renderer::Camera` and the winding the Make...Mesh functions use; a flip also reverses the winding), with `--scale` for another unit; node transforms are not baked into meshes except through `--bake-nodes` (default: one mesh resource per glTF mesh in its own space, the scene structure is Day 338's `level` output).
- [ ] **Morph targets** (`targets`, `weights`): refused with the primitive named, unless `--drop-morph-targets` imports the base shape (counted warning); the mesh format has no morph section (Day 319).
- [ ] **Skin attributes:** `JOINTS_0` and `WEIGHTS_0` map to the mesh skin stream (the four-largest rule of Day 319); `JOINTS_1` and up (more than four influences) keep the four largest with a counted warning; the skeleton reference is filled by Day 337.
- [ ] **Mesh minting path:** the library builds `MeshBuildInput` and calls `BuildMeshBlob` (Day 319): what is imported is exactly what `.mesh.src` would mint; the tool's `--emit-src` option (Day 338) writes the equivalent `.mesh.src` for small meshes (tests and inspection), refusing meshes over 10,000 vertices.
- [ ] **`ImportIntoBackend` and the in-memory loop:** the imported meshes are added to a `HostMemoryBackend` under their suggested paths (`/<glTF name>/meshes/<mesh name>`), mounted with `ResourceManager::MountBackend`, and loaded by path and by id through the manager (Days 328-330), including the asynchronous load with the closure rule; a re-import into the same backend with the manifest of the first replaces the blobs under the same ids (the backend's `Replace`, Day 315) and the manager's `PollChanges` reloads them.
- [ ] **Malformed meshes:** truncated or oversized primitives, indices past the vertex count, a primitive with no `POSITION`, NaN positions, a vertex count over the limit, a strip with fewer than three indices; each refused with the mesh and primitive named, no crash under asan.
- [ ] **Tests:** the fixtures of Day 335's writer (a triangle, a cube with normals and UVs in one primitive with three slots, a mesh with sparse accessors, strips and fans, a skinned mesh): each imported and compared with the blob minted from the equivalent `.mesh.src`; import twice gives byte-identical blobs and manifest; re-import after reordering the file's meshes keeps the ids; the in-memory loop above end to end on a null device.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (the importer parses hostile files); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. Importer code that is not behind the JSON or image-decoder wrappers uses the engine's types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 341; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels; `docgen lint` stays at zero; `cmake --build build --target docs` output committed; the guide page `docs/guide/assets.md` gets the "Importing glTF" section (this phase's three days add to it) and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** the importer lives in `engine/asset_import/`, a leaf module nothing else in the engine depends on (the Day 339 gate checks it); a new target or edge updates `ARCHITECTURE.md` and `tools/module_deps_check/modules.txt` in the same commit; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a `.glb` held in memory imports to mesh blobs that equal what the `.mesh.src` path would mint, they load through the resource manager from a `HostMemoryBackend`, hostile meshes are refused naming the glTF object, ids survive a re-import, and the output is deterministic.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
