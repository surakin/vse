# Phase 27 / Day 449 — The LuaJIT Backend II: The Sandbox and the Budgets

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Day 448.

Scripts are untrusted input: a bad script cannot hang, exhaust memory or reach the operating system.

## Tasks

- [ ] The whitelist environment the spike settled: each state starts with only the allowed libraries, `loadstring`/`load` limited to text chunks (no bytecode from scripts), `require` replaced by the engine's asset-id loader (Day 463), `print` going to the engine log; `io`, `os`, `package`, `debug`, `ffi` and the `jit` control functions are absent; the globals table of the engine is read-only to scripts (a metatable that refuses writes), each script's own environment writable.
- [ ] The instruction budget by `lua_sethook` with a count and the wall-clock budget checked in the same hook; exceeding either aborts the call with `BudgetExceeded` (the script's state is discarded or reset by policy: a setting `script.on_budget`: `reset` default, `disable`, `log`); the spike's finding on the JIT (a hooked state does not JIT-compile the hooked traces) is recorded and the budget is a per-call opt-in with a default for scripts' ticks.
- [ ] The memory cap through the allocator: an allocation past the cap fails, Lua raises its own memory error, the call returns `OutOfMemory`, and the state stays usable (the cap's accounting survives the failure).
- [ ] Deep recursion, string-doubling, `__gc` and coroutine-that-never-yields cases of the hostile corpus all end with a status in bounded time and memory, in plain, asan and tsan.
- [ ] `math.random` and `os.time`-like facilities: `math.random` is seeded by the engine per state from a setting (deterministic runs); no wall-clock or environment access reaches a script except through the time binding (Day 453).
- [ ] The conformance suite's budget cases now run and pass for LuaJIT.
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** A script that loops, allocates or recurses without end is stopped with a status and the engine continues; the sandbox exposes no operating-system facility.
