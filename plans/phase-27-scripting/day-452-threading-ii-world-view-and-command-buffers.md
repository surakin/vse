# Phase 27 / Day 452 — Threading II: The World View, Command Buffers and the Ordered Merge

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Day 451; Phase 17.

How scripts running at the same time see and change the world without a lock and without a race.

## Tasks

- [ ] The access model the Day 444 spike chose, built: during a script phase the ECS is **read-only** to every partition (structural changes forbidden, enforced by the Phase 17 iteration guard); a script reads other entities' components as of the tick start; its own entity's writes are visible to itself at once through a per-partition overlay; every write, spawn, destroy, component add/remove and event goes into the partition's **command buffer** (a bump-allocated, typed, versioned buffer in the engine's arena).
- [ ] The merge at the sync point: the buffers are applied in the order `(partition id, entity stable id, sequence number)`, a total order independent of thread count and timing; conflicts (two partitions write the same component of one entity) resolve by that order and are counted and logged in debug (`script.conflicts` setting: `last_wins` default, `error`, `merge_vec3` not offered).
- [ ] Entities spawned by a script get ids reserved from per-partition blocks at the phase start, so a script can use the new handle in the same tick before the merge creates the entity (a command refers to a reserved handle); the ids are the same for any thread count.
- [ ] Reads of an entity destroyed earlier in the same tick return the tick-start value (documented); a read of an entity destroyed in a previous tick fails with the handle error.
- [ ] A test world with 10,000 entities and scripts that move, spawn and destroy each other: the final ECS state hash is equal at 1, 2, 7 and 16 threads and across 100 runs; a planted-nondeterminism test (a binding that reads a racing value) is caught by the hash comparison.
- [ ] tsan clean on the world-view tests; the command buffer's cost per command and the merge's cost per command recorded in the guide.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Scripts on many threads read a stable world, write through ordered command buffers, and the merged result is identical for any thread count.
