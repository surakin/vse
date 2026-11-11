# Phase 19 / Day 261 — Incremental Saves

**Phase:** [Phase 19 — Storage](../phase-19-storage.md)
**Depends on:** Day 260; Phase 17 (change stamps, the removal log).

A save of a large, mostly idle world costs what changed.

## Tasks

- [ ] **Saver state:** a `SaveSession` per (world, store) remembers the world tick of its last save; a save visits only chunks whose written-ticks for a registered column are newer, encodes the entities of those chunks, and puts only records whose content hash differs from the stored one.
- [ ] **Removals and structure:** destroyed entities come from the ECS removal log, a component added or removed shows as a changed record; if the removal log has overflowed since the last save (its capacity is bounded) the session falls back to a key-set difference against the store and reports it.
- [ ] **First save of a process:** without a remembered tick the session diffs by the stored per-record content hash, so unchanged entities are not rewritten after a restart; the hash and the rule are in the spec.
- [ ] **Equality property:** a full save and a sequence of incremental saves produce identical stored state (the same records, byte for byte).
- [ ] **Tests** (`tests/storage/incremental_test.cpp`): 500 random frames of mutation (change components, add and remove components, create and destroy entities, re-parent) with an incremental save every 7 frames; after each save the store equals a fresh full save of the same world; an idle world's incremental save touches no record (counter) and visits no unchanged chunk; removal-log overflow fallback; restart equivalence (new session, same store, nothing rewritten); cancel mid-save leaves the store at the previous save.
- [ ] **Cost record:** time of an idle-world incremental save at 100,000 entities and of one with 1 percent changed, in the day file (the pass condition is on Day 265).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors when the day touched concurrency, lifetime or memory code (most days of this phase do); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with Clang and GCC, unity on and off. New code uses the engine's wrappers and types, never `std` (`std_usage_check`).
- [ ] **Two machines (CLAUDE.md "Windows is a supported platform"):** the code is portable; it is built and tested on the Windows PC no later than Day 265, and until then the day is listed as "open until the other machine runs it" in `KNOWN-ISSUES.md`.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"):** public headers carry `///` comments with the contract labels (`docs/DOC-CONVENTIONS.md`); `docgen lint` stays at zero; `cmake --build build --target docs` output committed; new modules are added to `docs/reference/modules.txt`; the guide page named in the day is updated or created and its tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md "Module dependencies follow ARCHITECTURE.md"):** a new module or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt` and the CMake targets (with `_asan`/`_tsan` flavors) in the same commit; no upward edge, no cycle; every new test file has its line in `tests/phases.txt` (phase 19).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test that fails without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** saving a world writes only what changed and always leaves the same stored state as a full save.
