# Phase 22 / Day 333 — The `asset_mint` Tool I: Project Files, Mint, Pack, Verify, Incremental Minting & Determinism

**Phase:** [Phase 22 — Asset Pipeline & Resource System](../phase-22-asset-pipeline.md)
**Depends on:** Days 314-327 and 316.

One command-line tool that turns a source tree into blobs and a pak, the same way every time. (Split in two on 2026-11-14, the user's decision: the reference-safe edits, `remint`/`unmint`, `pack --target`, version enforcement and the level column-image question are Day 334.)

## Tasks

- [ ] **Skeleton and animation types in the tool (added 2026-11-13):** `adopt`, `mint`, `verify`, `list`, `remint` and `unmint` cover `.skel.src` and `.anim.src` (the minters and upgrade chains of Days 318 and 320 live here), a mesh source naming a skeleton and a clip source naming a skeleton are minted in dependency order and the incremental cache invalidates a clip when its skeleton source changes (the skeleton hash is part of the clip's input hash); `asset_mint list --skeletons` prints each skeleton's joint count, hash and the meshes and clips that use it; a test per new behavior.
- [ ] **Project file `assets.project`** (a text file: source root, output root, pak name, per-extension rules with options such as the texture format and quality, excluded paths); a parser with line-numbered errors.
- [ ] **Commands (part I):** `asset_mint adopt` (assign an id to a source that has none, writing it into the file; the only command that writes an id into a source), `mint` (all sources to blobs, in dependency order, every error reported), `pack` (blobs to a pak; refuses non-current or unverifiable blobs), `verify` (every blob and the pak: headers, hashes, **the dependency closure complete and typed, a base pak present by hash**; a hand-edited dependency section fails), `list` (ids, paths, types, sizes, read from the table; `--skeletons` as above; `mint` and `pack` also write `resources.table` and the pak's table section, and `verify` checks every table entry against the blobs' own ids) and `check` (the closure of the whole project: every reference resolves, every type and version matches, no cycle, no duplicate id). The commands of Day 334 (`copy`, `reid`, `refs`, `rename`, `move`, `delete`, `remint`, `unmint`) are not part of this day.
- [ ] **Incremental minting:** an output manifest records for each blob the hashes of its inputs, its dependencies' hashes, the tool version and the options; an unchanged blob is not rewritten; a changed texture re-mints the materials that name it only if the material blob embeds anything from it (it does not: ids only), so only dependents whose *inputs* changed are rebuilt; `--force` rebuilds everything.
- [ ] **Breaking a reference is caught (part I):** tests: a source deleted by hand makes the next `mint` fail naming every dependent (never a blob with a dangling id); two sources with one id (a plain `cp`) fail the mint naming both; a source with no id fails the mint naming `adopt`, and re-running `adopt` on an adopted file changes nothing; a hand-edited dependency section fails `verify`; `check` runs in the demo's ctest and in `ctest -L asset`. The tool-guarded changes (rename, move, delete, reid) and their tests are Day 334.
- [ ] **Parallel minting** on the job system with a deterministic result: outputs and manifests are independent of the worker count and of the file system's listing order (everything sorted by id).
- [ ] **Determinism tests** (`tests/asset/mint_determinism_test.cpp`): a sample project minted twice, with 1 and 8 workers, and after touching the source files' modification times, yields byte-identical blobs, manifest and pak; the tool version is part of the manifest so a version change re-mints; a path with a non-NFC name normalizes.
- [ ] **Exit codes and messages:** errors name the file and line; a mint error in one file does not stop the others (all errors reported, exit code non-zero); `--quiet` and `--verbose`.
- [ ] **Tool tests** through ctest scripts (`asset_mint_mint_sample`, `asset_mint_verify_sample`, `asset_mint_incremental`, `asset_mint_bad_source_exit_code`).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code; quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`); file access only through `io::` (`file_access_check`); settings only through `settings::Registry` (`config_access_check`).
- [ ] **Three machines (CLAUDE.md "Three machines"):** shared code is portable and checked on the Windows PC and the Mac no later than Day 340; until then the day is "open until the other machines run it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page `docs/guide/assets.md` is updated and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 22).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** a sample project mints, packs, verifies, lists and checks with one tool, a second run does nothing, and the output is byte-identical across worker counts.

## Outcome

(written when the day is done)

## Post-implementation code review

(written when the day is done)
