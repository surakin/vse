# Phase 27 / Day 451 — Threading I: Partitions, the State Pool and the Ownership Rules

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 448-450; Phase 2.

The core of the multithreaded design: which state runs where, and the rule that no state is ever used by two threads at once.

## Tasks

- [ ] `script::ScriptWorld`: `P` partitions (setting `script.partitions`, 1 to 1024, default 64), one `script::State` each, created lazily, with the same binding set and the same loaded scripts; `PartitionOf(StableId)` by a fixed hash so an entity's partition never depends on thread count, load or order of creation.
- [ ] The ownership rule in code: a partition's state is *checked out* by exactly one job per tick phase (an atomic owner token; a second checkout aborts in debug and returns `Busy` in release), and handed back at the phase's end; the debug owner check of Day 446 is wired to the job's worker id.
- [ ] Rules for shared data, enforced by what is installed: Lua objects never cross states (only `Value` copies); source text and minted blobs are immutable and shared by pointer (`SharedBytesPtr`); no engine singleton used by bindings is mutable without its own lock, listed in the guide's table of thread rules.
- [ ] Memory: a per-partition cap (`script.partition_memory`), a world total reported; a partition that reaches its cap is reset by policy (Day 449's `on_budget`) without touching the others.
- [ ] Per-partition statistics (calls, time, instructions, memory, budget hits) readable lock-free from any thread; the sum equals the world's.
- [ ] Tests: the same script loaded in 64 partitions, a checkout race test (several threads try to check out one partition, exactly one wins), a memory-cap test per partition, `PartitionOf` stable across runs and platforms (a table of committed values).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A script world owns `P` independent states with a checked one-owner rule and stable entity-to-partition assignment.
