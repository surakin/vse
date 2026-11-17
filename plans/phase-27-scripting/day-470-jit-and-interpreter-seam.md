# Phase 27 / Day 470 — The JIT-versus-Interpreter Platform Seam

**Phase:** [Phase 27 — Scripting](../phase-27-scripting.md)
**Depends on:** Days 445, 448-449.

The embedding already treats the JIT as a platform choice; this day makes both modes tested.

## Tasks

- [ ] `script.jit` (setting and `ENGINE_LUAJIT_JIT` build option): on, off for a state (`jit.off()` from the backend at creation) or compiled out (`LUAJIT_DISABLE_JIT` for platforms that forbid run-time code generation: iOS App Store builds, consoles); the capability `jit` of the backend reports the truth.
- [ ] The whole script suite runs in both modes (a ctest flavor `script_nojit` running the Phase 27 tests with the JIT off; the results must be identical, only the speed differs); a test proves a JIT-off state compiles no machine code.
- [ ] A platform table in the guide: which platforms run which mode (Linux, Windows and macOS: JIT on by default; iOS and consoles: interpreter only, decided in Phases 29 and 30; Android: JIT allowed, the day-level decision is Phase 29's), with the build flags.
- [ ] The JIT's memory behavior with `P` states: the per-state code cache size cap (`jit.opt` parameters set from a setting), the memory totals in the statistics, a test with 256 partitions all JIT-compiling the same hot loop.
- [ ] The budget-and-JIT interaction of Day 449 re-measured and the recommendation recorded (the instruction budget on or off for update callbacks).
- [ ] **Runs (CLAUDE.md "Tests run by phase"):** `scripts/test-phase.sh --affected` plain; then the asan and tsan flavors (this phase runs script code on several threads on nearly every day, so tsan is not optional); quote each run's final `ran X of Y tests (phases ...)` line in this file and never say "all tests pass" for a reduced run. `ctest -L docs` when a public header or a guide changed. Build clean (warnings are errors) with both compilers and unity on and off; `-DENGINE_BUILD_SHARED=ON` and `-DENGINE_LIBRARY_WITH_IMPORTER=OFF` still build.
- [ ] **Three machines (CLAUDE.md):** built and tested on the machine the day is done on (Linux); shared code (CMake, the gates, tests, headers) is checked on the Windows PC and the Mac no later than Days 473-474, and `KNOWN-ISSUES.md` lists what waits for those runs.
- [ ] **Documentation (CLAUDE.md "Public API is documented before a day is done"), when a public header changed:** `///` comments with the contract labels and `docgen lint` at zero; `cmake --build build --target docs` output committed; `docs/guide/scripting.md` updated or created; tagged samples compile and run.
- [ ] **Module rules (CLAUDE.md):** a new module, target or edge updates `ARCHITECTURE.md`, `tools/module_deps_check/modules.txt`, `tests/module_map.txt` and `BUILD.md` in the same commit; every new test file has its line in `tests/phases.txt` (phase 27); only wrapped types, no `std`, no raw platform macros; Lua headers only in the LuaJIT backend files (the boundary gate).
- [ ] **Day review (CLAUDE.md, required):** `/code-review high` over the day's diff after the scoped tests; verify, fix (with a regression test shown to fail without the fix) or decline each finding with a reason; record the count and outcome under "Post-implementation code review" below and in `STATUS.md`.
- [ ] `STATUS.md` and `KNOWN-ISSUES.md` updated in the same commit; commit and push.

**Done:** Both execution modes pass the same tests, and the JIT is a configuration choice with a documented platform table.
