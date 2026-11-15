# Phase 23 / Day 351 — C API: glTF Import at Run Time

**Phase:** [Phase 23 — The Engine as a Loadable Library](../phase-23-engine-as-library.md)
**Depends on:** Day 350 and Phase 22 Days 335-339.

`engine_import_gltf` for a host such as the host: bytes in, resources in the memory backend, the manifest out.

## Tasks

- [ ] Build option `ENGINE_LIBRARY_WITH_IMPORTER` (default ON in the shared library for hosts, OFF otherwise): links `asset_import` (and with it the image decoders) only then; the C API reports `engine_caps` bit `IMPORT_GLTF`; `asset_boundary_check` gets the allow-list entry for `engine/engine_c/` with its reason, and a gate rule that nothing else in the library includes the importer.
- [ ] `engine_import_gltf(engine*, bytes, size, const engine_import_options*, engine_import_result*)` (synchronous) and `engine_import_gltf_async(...) -> engine_ticket*` (poll, progress, cancel, finish); external URIs go through a host callback `read_file(name, max_bytes, buffer_out)` (the `GltfSource`), the options carry limits (defaults from the `assets.import.*` settings), scale, prefix, the manifest bytes of an earlier import; the result lists the resource ids and paths and returns the new manifest as an `engine_buffer`.
- [ ] Adding the result to the memory backend (atomic, as `ImportIntoBackend`), with the report's warnings returned as strings.
- [ ] Tests (C host): import the fixture `tests/asset/data/gltf/model.gltf` from memory, render nothing, load every mesh, skeleton and clip, re-import with the returned manifest and check the ids are kept, import asynchronously and cancel, feed hostile bytes (a mutated glb, an oversized image) and get a status and a message, never a crash; the build without the importer returns `ENGINE_STATUS_UNSUPPORTED`.
- [ ] `docs/guide/engine-library.md`: the the host path end to end.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (this phase does, nearly every day); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers, unity on and off, **and with `-DENGINE_BUILD_SHARED=ON`** from Day 343 on.
- [ ] **Three machines (CLAUDE.md):** this day is built and tested on the Linux machine; shared code (CMake, the gates, the C header, the loader) is checked on the other two no later than Day 357; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** headers carry `///` comments with the contract labels (`engine_c.h` has its own comment convention, set on Day 346, checked by `docgen` or its own gate); `docgen lint` at zero; `cmake --build build --target docs` output committed; the guide `docs/guide/engine-library.md` is updated.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and `BUILD.md` (a new prerequisite) in the same commit; every new test file has its line in `tests/phases.txt` (phase 23).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a C host imports a glTF from memory into the engine's content, with ids kept across imports, asynchronously if it wants, and hostile files refused cleanly.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
