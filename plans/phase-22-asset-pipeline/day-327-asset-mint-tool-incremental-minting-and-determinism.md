# Phase 22 / Day 327 — The `asset_mint` Tool: Project Files, Incremental Minting & Determinism

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Days 314-322 and 316.

One command-line tool that turns a source tree into blobs and a pak, the same way every time.

## Tasks

- [ ] **Project file `assets.project`** (a text file: source root, output root, pak name, per-extension rules with options such as the texture format and quality, excluded paths); a parser with line-numbered errors.
- [ ] **Commands:** `asset_mint mint` (all sources to blobs), `asset_mint pack` (blobs to a pak), `asset_mint verify` (every blob and the pak: headers, hashes, **the dependency closure complete and typed, a base pak present by hash**), `asset_mint list` (ids, paths, types, sizes, read from the table; `mint` and `pack` also write `resources.table` and the pak's table section, and `verify` checks every table entry against the blobs' own ids), `asset_mint refs <asset>` (what references it and what it references), `asset_mint rename` and `asset_mint move` (change the alias, never the id; nothing else to update), `asset_mint delete` (**refused while anything references the asset**, listing the referrers; `--with-dependents` lists what would have to go first and still does nothing without the user removing the references), `asset_mint check` (the closure of the whole project: every reference resolves, every type and version matches, no cycle, no duplicate id), `asset_mint remint` (rewrite old-version blobs to the current version: the upgrade chains of every type live in this tool, implemented from Day 317 on; the result must be byte-identical to a fresh mint of the same source where the source is available, and is verified so), `asset_mint unmint` (blob back to source text for levels and materials).
- [ ] **Incremental minting:** an output manifest records for each blob the hashes of its inputs, its dependencies' hashes, the tool version and the options; an unchanged blob is not rewritten; a changed texture re-mints the materials that name it only if the material blob embeds anything from it (it does not: ids only), so only dependents whose *inputs* changed are rebuilt; `--force` rebuilds everything.
- [ ] **Breaking a reference is impossible through the tool, and caught if done outside it:** tests: rename and move leave every dependent's blob bytes unchanged (ids only); delete of a referenced asset exits non-zero with the referrers named and changes nothing; a source deleted by hand makes the next `mint` fail naming every dependent (never a blob with a dangling id); two sidecars with one id fail the mint; a hand-edited dependency section fails `verify`; `check` runs in the demo's ctest and in CI-style `ctest -L asset`.
- [ ] **Parallel minting** on the job system with a deterministic result: outputs and manifests are independent of the worker count and of the file system's listing order (everything sorted by id).
- [ ] **Version enforcement:** `verify --require-current` fails (non-zero exit, names every old blob and the fix) when any blob or pak is not the current version of its type; `pack` refuses non-current or unverifiable blobs; the manifest records every type's format version so a bump rebuilds all blobs of that type on the next `mint`; ctest `asset_mint_old_versions_fixture` runs `verify --require-current` over a fixture directory with one blob of every older version of every type (it must fail naming all of them), then `remint` (it must succeed), then `verify --require-current` (it must pass) and compares with a fresh mint.
- [ ] **Determinism tests** (`tests/asset/mint_determinism_test.cpp`): a sample project minted twice, with 1 and 8 workers, and after touching the source files' modification times, yields byte-identical blobs, manifest and pak; the tool version is part of the manifest so a version change re-mints; a path with a non-NFC name normalizes.
- [ ] **Exit codes and messages:** errors name the file and line; a mint error in one file does not stop the others (all errors reported, exit code non-zero); `--quiet` and `--verbose`.
- [ ] **Tool tests** through ctest scripts (`asset_mint_mint_sample`, `asset_mint_verify_sample`, `asset_mint_incremental`, `asset_mint_bad_source_exit_code`).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 330; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a sample project mints, packs and verifies with one tool, a second run does nothing, and the output is byte-identical across worker counts.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
