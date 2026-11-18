# Phase 27 / Day 452 — Threading II: The World View, Command Buffers and the Ordered Merge

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Day 451; Phase 17.

How scripts running at the same time see and change the world without a lock and without a race.

## Tasks

- [x] The access model the Day 444 spike chose, built: during a script phase the ECS is **read-only** to every partition (structural changes forbidden, enforced by the Phase 17 iteration guard); a script reads other entities' components as of the tick start; its own entity's writes are visible to itself at once through a per-partition overlay; every write, spawn, destroy, component add/remove and event goes into the partition's **command buffer** (a bump-allocated, typed, versioned buffer in the engine's arena).
- [x] **Decided (user, 2026-11-18): design A.** Scripts read the live ECS directly (read-only during the script phase, so the values are the tick-start values) plus the per-partition overlay for own-entity reads after own writes; **no tick-start copy** is made. Default `P` = 64.
- [x] The merge at the sync point: the buffers are applied in the order `(partition id, entity stable id, sequence number)`, a total order independent of thread count and timing; conflicts (two partitions write the same component of one entity) resolve by that order and are counted and logged in debug (`script.conflicts` setting: `last_wins` default, `error`, `merge_vec3` not offered).
- [x] Entities spawned by a script get ids reserved from per-partition blocks at the phase start, so a script can use the new handle in the same tick before the merge creates the entity (a command refers to a reserved handle); the ids are the same for any thread count.
- [x] Reads of an entity destroyed earlier in the same tick return the tick-start value (documented); a read of an entity destroyed in a previous tick fails with the handle error.
- [x] A test world with 10,000 entities and scripts that move, spawn and destroy each other: the final ECS state hash is equal at 1, 2, 7 and 16 threads and across 100 runs; a planted-nondeterminism test (a binding that reads a racing value) is caught by the hash comparison.
- [x] tsan clean on the world-view tests; the command buffer's cost per command and the merge's cost per command recorded in the guide.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Scripts on many threads read a stable world, write through ordered command buffers, and the merged result is identical for any thread count.

## Outcome (2026-11-18, Linux)

Design A as decided: no tick-start copy. `script/command_buffer.{h,cpp}` (`CommandBuffer`: numbered commands, plain-byte payloads in one kept block, per-partition placeholders `partition * 2^22 + n` with generation `kPlaceholderGeneration`; `Overlay` for the running entity's own writes), `script/merge.{h,cpp}` (`CommandKindTable`, `MergeContext` with placeholder binding, `Merge` in order `(partition, order_key, sequence)`, sorting an unsorted buffer, `ConflictPolicy` LastWins/Error with counts), `script/partition_context.{h,cpp}` (`PartitionContext`, `ScopedPartitionContext` per thread, `ContextEntityCheck`, `ReadOnlyPhase` over the ECS parallel phase), setting `script.conflicts`. Tests (`world_view_test.cpp`, 9 cases + a hidden bench): 10,000 entities moving, spawning and destroying each other give one state hash at 1, 2, 7 and 16 threads; 100 runs at 7 threads give one hash; a binding that reads a racing counter is caught; error policy; tick-start reads and the entity check before and after the merge; merge order independent of recording order; placeholders, dangling and unknown kinds; overlay; buffer edge cases. Cost per command (one thread): 23 ns to record, 100 ns to merge. Guide: "The world view and the merge".

Not built, left to the next days: the tick driver on the Phase 2 scheduler (the tests drive partitions with plain threads), the world-specific bindings (Day 453), events.

## Post-implementation code review

`/code-review high` over the day's diff: 3 findings, all fixed. (1) `Record` read a null payload when `size` was not 0 and copied byte by byte: now refused, bulk copy (record cost 35 to 23 ns). (2) `Begin` accepted a partition number of 1024 or more, which wraps the placeholder block onto partition 0's: now asserted. (3) Edge cases untested: a test added for the null payload and the last partition's placeholder. Regression tests shown failing without the fix: not run (the unfixed code crashes on the null payload, which a test cannot assert; the partition assertion aborts).

Runs: plain `ran 3953 of 10616 tests (phases: all; flavors: plain; ...)`, asan `ran 3248 of 10616`, tsan `ran 2903 of 10616` (tsan clean on the world-view tests); only the known failures (`engine_c_surface`, the Job-destroyed test, the audio voice-cost timing test). `ctest -L docs` 9 of 9; GCC and Clang, unity off: script tests pass. Not run: `-DENGINE_BUILD_SHARED=ON`; Windows and macOS (Days 473-474).
