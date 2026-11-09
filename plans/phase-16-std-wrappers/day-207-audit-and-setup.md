# Phase 16 / Day 207 — Audit, Design Confirmation & Phase Setup

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Phase 15 complete (Day 206).

Take the exact inventory the wrapper modules are sized from, confirm the design with it, and set the phase up. Planning-level numbers are in `docs/superpowers/specs/2026-11-09-std-wrappers-design.md`.

## Tasks

- [ ] `tools/std_usage_report` (report only, ctest-free, not a gate yet): scans `engine/`, `game/`, `tools/`, `tests/` (not `third_party/`), ignoring comments and string literals, and prints per standard header and per `std::` name (and per unqualified C-library name such as `strlen`, `memcpy`) the count and the files, grouped by top directory and by module; deterministic output, self-test with a planted fixture.
- [ ] Run it and commit its output as the inventory in the spec (section 2 replaced by the exact table); list every `std::` name that has no planned wrapper and decide each (wrap, or remove the use, or document as compiler-mandated).
- [ ] Decide with the measurement: which lower layers (`os`, `mem`, `log`) use scalar math, formatting and C-string functions, and therefore where `scalar.h`, `cstr` and the `Print` functions live; record the layer of every new module and add them as planned rows to `ARCHITECTURE.md` (marked planned until their day).
- [ ] Record the baselines the phase must not regress: the job-system benchmark, the log benchmark and the container benchmark, in plain and release flavors, in the spec (command, machine, numbers, noise band from five runs).
- [ ] List the Windows-only files that use the standard library (`engine/platform/windows`, `engine/rhi/dx12`, `win32_*`, `tests/windows`) in the spec with the count per file; they are the input to Day 219.
- [ ] Spec amendments from the findings; `PLAN.md` assumption #2 gets a new numbered assumption (#23) "One wrapper module per standard-library category" (the rule, the exceptions, the reversal of the permissive text); `CURRENT_PHASE` becomes 16 in this day's commit; `KNOWN-ISSUES.md` gains the entry "standard library outside the wrappers (Phase 16)" with the counts.
- [ ] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** the spec carries an exact, tool-made inventory and the baselines, every `std::` name has a decision, `ARCHITECTURE.md` lists the planned modules, the report tool has a self-test, and Days 208-221 are still accurate (or amended in this commit).
