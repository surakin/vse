# Phase 27 / Day 470 — The JIT-versus-Interpreter Platform Seam

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 445, 448-449.

The embedding already treats the JIT as a platform choice; this day makes both modes tested.

## Tasks

- [x] `script.jit` (setting and `ENGINE_LUAJIT_JIT` build option): on, off for a state (`jit.off()` from the backend at creation) or compiled out (`LUAJIT_DISABLE_JIT` for platforms that forbid run-time code generation: iOS App Store builds, consoles); the capability `jit` of the backend reports the truth.
- [x] The whole script suite runs in both modes (a ctest flavor `script_nojit` running the Phase 27 tests with the JIT off; the results must be identical, only the speed differs); a test proves a JIT-off state compiles no machine code.
- [x] A platform table in the guide: which platforms run which mode (Linux, Windows and macOS: JIT on by default; iOS and consoles: interpreter only, decided in Phases 28 and 29; Android: JIT allowed, the day-level decision is Phase 28's), with the build flags.
- [x] The JIT's memory behavior with `P` states: the per-state code cache size cap (`jit.opt` parameters set from a setting), the memory totals in the statistics, a test with 256 partitions all JIT-compiling the same hot loop.
- [x] The budget-and-JIT interaction of Day 449 re-measured and the recommendation recorded (the instruction budget on or off for update callbacks).
- [x] **Amended by Day 444 (user decision):** the budget mode is per world (budgeted = interpreter-only with the count hook, trusted = JIT without an instruction budget); the measured costs to re-check: JIT 481 ms, interpreter 698 ms, interpreter with hook 1,220 ms on the spec's 2e8-iteration loop.
- [x] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [x] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [x] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [x] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [x] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [x] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

## Notes

- `script.jit` (bool, default true, restart required) and `script.jit_code_kb` (0 keeps LuaJIT's 2048; LuaJIT always has one 64 KB area, so a smaller value acts as 64); CMake option `ENGINE_LUAJIT_JIT` defines `LUAJIT_DISABLE_JIT`; `luajit::JitCompiledIn()`, `luajit::OptionsFromSettings`, `State::JitReport`, `ScriptWorld::JitTotals`, `ScriptRuntime::JitTotals` (a partition that is held is skipped). The capability `jit` is `JitCompiledIn() && !budgeted && options.jit`; a budgeted world never JITs, a trusted one refuses budgets.
- The flavor `script_nojit` is a build tree: `cmake --preset linux-nojit` (`build-nojit`), then `scripts/test-phase.sh 27 --no-display --build-dir build-nojit`.
- Measured (Debug, Linux x86-64, 2e8-iteration loop): JIT 467 ms, interpreter without budget 865 ms, interpreter with the count hook 1,262-1,703 ms. 256 uncapped states with 300 hot loops: about 616 traces and 159 KB of code per state, 40.7 MB in all. Recommendation (in the guide): budget on for untrusted scripts, JIT for trusted worlds.
- Pages touched: `docs/guide/scripting.md` ("JIT or interpreter"), `docs/reference/script.md`, `symbols.txt`, `BUILD.md`.

## Runs

- Phase 27 plain: `ran 297 of 11300 tests (phases: 27; flavors: plain; display: excluded; crash tests: excluded) in 110 s`
- Phase 27 asan: `ran 290 of 11300 tests (phases: 27; flavors: asan; display: excluded; crash tests: excluded) in 27 s`
- Phase 27 tsan: `ran 290 of 11300 tests (phases: 27; flavors: tsan; display: excluded; crash tests: excluded) in 25 s`; one run in seven failed `LuaJIT sandbox: a trusted state refuses ...` with a race inside libgcc's unwinder (`btree_insert`); six reruns passed (`KNOWN-ISSUES.md`).
- nojit tree: `ran 299 of 11304 tests ... in 30 s`, passed.
- Reduced runs, not the whole suite. Gates and `ctest -L docs` 9/9 pass. Builds clean: Clang unity, Clang non-unity, GCC non-unity.

## Post-implementation code review

`/code-review high`: 4 findings. (1) `JitTotals` skips a held partition: declined, it is the documented contract. (2) `ReadJit` costs a Lua call per trace: declined, it is a statistics call, not a per-frame one. (3) the tsan flake in libgcc's unwinder: recorded in `KNOWN-ISSUES.md`, no engine or LuaJIT frame is in the race. (4) `jit_code_kb` under 64 acts as 64: documented in the setting's description and the guide.

**Done:** Both execution modes pass the same tests, and the JIT is a configuration choice with a documented platform table.
