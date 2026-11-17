# Phase 27 / Day 462 — Minted Script Assets I: The Format and the Minter

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Phase 22; Days 446, 449.

Scripts are assets like meshes and textures: minted, validated, hash-identified, deterministic.

## Tasks

- [ ] The script blob format in the Phase 22 container (`SCRP`): source text (UTF-8, normalized line endings), the language and its version, the entry points the validator found, the list of asset ids it depends on, a content hash; versioned, refused by value when truncated or from another version; zero-copy loads of the source as `SharedBytes`.
- [ ] `asset_mint script` (a new command): reads `.lua` sources and a manifest (asset id, dependencies), **validates** each by compiling it in a sandboxed state (syntax errors with file and line, banned library use found statically where cheap, a check that declared lifecycle functions are functions), and writes the blobs; minting is deterministic (same inputs and tool version give byte-identical blobs; a test mints twice and compares).
- [ ] Dependencies are asset ids, not file paths: `require("assets.player")` in a script is rewritten by the minter's manifest into an id lookup; a cycle or a missing dependency fails the mint with the chain printed.
- [ ] Optional bytecode (the Day 444 decision): `asset_mint script --bytecode` adds an architecture- and LuaJIT-version-keyed bytecode section next to the source; the loader uses it only if the key matches and falls back to the source otherwise (never required, never trusted without the key); its determinism is tested per architecture.
- [ ] The minter's own tests and fixtures (valid scripts, each class of invalid script), a fuzz test of the loader of the blob (hostile input), the format described in `docs/guide/assets.md`.
- [ ] The no-tools-in-the-game gate (Phase 22) lists the new minter.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Scripts are minted into validated, deterministic blobs with id-based dependencies and an optional bytecode cache.
