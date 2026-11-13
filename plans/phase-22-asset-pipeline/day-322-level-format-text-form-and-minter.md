# Phase 22 / Day 322 — The Level Format, Its Text Form & the Level Minter

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Days 314-321; Phase 19 (`storage::ComponentRegistry`, `WorldStorage`, entity keys).

Levels as the storage module's component records plus resource references; the one text form of the phase.

## Tasks

- [ ] **Level blob v1:** a header, the entity table (keys from `EntityKeyComponent`), the component records encoded by the `ComponentRegistry` (the same canonical encoding the saves use: one set of field codecs), a reference table (every `ResourceId` a component names, by field), and a dependency list.
- [ ] **Column images for bulk loading:** components that are trivially copyable plain data are stored as one memory image per component type (the array of the entities' values, little-endian, with a layout fingerprint of the type recorded in the blob and checked at load: a mismatch is a mint-version error, fixed by re-minting, never converted); other components use the registry's field codecs. Whether the ECS can adopt a column image into an archetype chunk without per-entity work is Day 313's spike 5; if it cannot, the loader does one bulk copy per column, never per field.
- [ ] **Resource references as components:** `MeshRenderer` is stored with its mesh and material as `ResourceRef` fields (a `ResourceId`), not as pointers; the registry gets a `ResourceRef` field kind, and the loader resolves the references through the manager on Day 323.
- [ ] **Text form `.level.src`:** line-oriented (`entity <name>`, then `component <Type>` lines with `field = value` lines, references by virtual path, comments, includes by path); a parser with line-numbered errors that uses the registry's field names and types so a new component needs no parser change; a formatter that prints a loaded world back to the canonical text (stable order) so `mint` then `unmint` round-trips.
- [ ] **`LevelMinter`:** text to blob; unknown component or field names are errors listing the registry's names; the dependency list is the closure of the references.
- [ ] **Loader `LoadLevelBlob(bytes, registry, world)`:** creates entities, decodes components, resolves references to ids (the manager resolves ids to resources on Day 323); the world is untouched when the blob is bad (decode into a staging buffer first, as `WorldStorage::Load` does).
- [ ] **Tests:** a level of 1,000 entities round-trips through text, blob and world and back to identical text; every parse error with its line; unknown fields; a reference to a missing resource is a mint error; hostile blobs; two mints identical.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 330; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a `.level.src` file mints to a blob, loads into a world, and prints back to the same text; unknown names and missing references are mint errors.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
