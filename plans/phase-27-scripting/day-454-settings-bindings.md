# Phase 27 / Day 454 — Settings Bindings

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Day 450; Phase 11.

Scripts read and change settings through the settings system and nothing else.

## Tasks

- [x] `settings.get(key)` typed from the registry, `settings.set(key, value)` validated by the registry (an out-of-range or wrongly typed value is a script error, nothing stored), `settings.exists`, `settings.describe(key)`; secret settings are not readable.
- [x] `settings.on_change(prefix, fn)`: prefix listeners delivered as events at the sync point in the merge order (never in the middle of a tick, never on the settings thread), unsubscribed when the script or entity is destroyed.
- [x] Thread rules: reads are `pure` (they read the settings snapshot taken at the tick start); writes go through the command buffer and apply at the sync point; a script never opens the configuration file or reads the environment (the sandbox has no `os`).
- [x] A per-script allow-list of key prefixes a script may set (default: `game.*`, `script.*` read-only), so a content script cannot change `renderer.*` unless the project says so; refused writes are errors with the key.
- [x] Tests: typed round trips, validation refusals, secret refusal, listener delivery order across partitions, the allow list.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Scripts read and write settings by dotted key with validation, an allow list and ordered change events.

## Notes

- `settings.on_change` takes the global function's name (a Lua function never leaves its state); the calls themselves are made by the event delivery of Day 459, which uses `CollectSettingChanges`. A merge command kind can now be `global` (no entity target, no conflict tracking). Default policy: scripts may set `game.*` only.
- Tests: `tests/script/settings_bindings_test.cpp` (9 cases): typed reads, errors, validation refusals, secret refusal, the allow list, merge application, notification order, subscription limits.

## Post-implementation code review

`/code-review high` over the day's diff: 1 finding, fixed. A script could call `settings.on_change` in a loop and grow its partition's subscription list without bound: capped at 32 per entity and 4096 per partition (test added, fails without the cap). Checked and fine: payload decoding re-validates sizes; a refused write stores nothing; secrets never reach an error message.

Runs: scoped to phase 27 (not `--affected`): plain `ran 113 of 10712 tests (phases: 27; flavors: plain; display: excluded; crash tests: excluded)`, asan `ran 106 of 10712`, tsan `ran 106 of 10712`; the gate checks (`*_check`, `test_phase_labels`, `docs_*`) 33 of 33 and `ctest -L docs` 9 of 9. Not run: the whole `--affected` selection (the user asked to skip rendering and window tests; run at the end of Day 467), Windows, macOS, `ENGINE_BUILD_SHARED=ON`.
