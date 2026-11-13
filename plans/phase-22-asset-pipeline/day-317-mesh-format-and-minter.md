# Phase 22 / Day 317 — The Mesh Format, Its Source Form & the Mesh Minter

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Days 314-316; Phase 5 (`renderer::MeshData`, `VertexLayoutData`).

The first typed resource, end to end: source text to blob to `MeshData`.

## Tasks

- [ ] **Mesh blob v1 (laid out for the GPU: the vertex and index sections are the exact bytes the RHI buffers hold, aligned for upload, no conversion at load):** sections for the vertex layout (the attribute list `MeshData::Set` takes), vertex bytes, index bytes (16 or 32 bit), topology, bounds, LODs (a list of index ranges with a screen-size threshold), material slots (a name and a range of indices per slot); every count bounded by a limit in the loader.
- [ ] **Source form `.mesh.src`:** a line-oriented text file (`attribute`, `v`, `i`, `lod`, `slot` records) defined in the guide with an example; a parser with line-numbered errors; no OBJ or glTF reader (the user's answer).
- [ ] **`MeshMinter`:** source to blob; computes bounds; reorders indices for the post-transform cache and vertices for fetch locality by a deterministic algorithm (ties broken by index), quantizes or packs attributes into the layout the shaders read (so nothing is converted at load), and validates fully (everything `MeshData::Set` checks, plus bounds and LOD ranges) so the runtime need not; validates through `MeshData::Set`'s own rules so what is minted loads.
- [ ] **Loader `LoadMeshBlob(bytes) -> MeshAssetData`** (CPU side: `MeshData` plus LODs and slots) through `BlobReader` (current version only): bounds-checked, then spans into the buffer, no copy of the vertex or index data into an intermediate `MeshData` (the trusted entry Day 313's spike chose; the renderer change, if any, is part of this day); the GPU object is made by the manager on Day 323.
- [ ] **Tests:** a table of source files (valid, and each error with its line number); the minted blob of a cube and of a sphere loads to the same `MeshData` the `Make...Mesh` functions build (bounds, counts, bytes); LOD and slot ranges are checked against the index count; a v0 golden blob (a deliberately older layout kept as a fixture) is refused by the loader, and `asset_mint remint`'s mesh upgrade (implemented in the tool this day) rewrites it to bytes identical to a fresh mint of the same source; hostile blobs (counts that overflow, ranges past the end) refused; minting twice is byte-identical.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 330; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a `.mesh.src` file mints to a blob that loads into the same `MeshData` as the built-in shapes, hostile blobs are refused, and the output is deterministic.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
