# Phase 16 / Day 221 — Docs, Closing Full Run & Wrap-up

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Days 207-220.

Close the phase: guides, the complete suite, the benchmarks, the uncapped review.

## Tasks

- [ ] Guide pass: a "Which wrapper do I use?" table at the top of `docs/guide/foundation-types.md` (every standard facility -> the engine name), pages `foundation-sync.md`, `foundation-cstr.md`, `foundation-math.md`, `foundation-os.md` read end to end; tagged samples compile; `docgen lint` zero; `cmake --build build --target docs` and `ctest -L docs`.
- [ ] Benchmarks: job-system, log, container, `renderer_bench` and `ui_bench` against the Day 207 baselines; table in this file; any regression beyond the noise band is fixed or recorded in `KNOWN-ISSUES.md` with numbers.
- [ ] **Standing policy (the phase's last act, after the gate is green):** `CLAUDE.md` gets a permanent section "Always use the wrapped types, never `std` (standing policy, required)": from now on every new or changed line in `engine/`, `game/`, `tools/` and `tests/` uses the engine's wrapper (`Atomic`, `Mutex`, `Move`, `MemCopy`, `FormatTo`, `Sqrt`, ...) and never includes a standard header or spells `std::`; a missing wrapper is added to its category's module first (with tests and docs), not bypassed; the only exceptions are the gate's documented list; the section links the "Which wrapper do I use?" table and the `std_usage_check` gate. The same rule goes into `PLAN.md` assumption #23 (final wording), a feedback memory in `claude-memory/` (with `MEMORY.md` line), and the "Hard architectural constraints" bullets that still say the standard library is allowed are removed.
- [ ] **Closing full run (CLAUDE.md):** `scripts/test-phase.sh --all --flavor all` on Linux, `ctest -L docs`, `rhi_demo`, `renderer_demo`, `ui_demo` 1000 frames with `--validation` on Wayland and X11; the Windows run from Day 219 repeated on the final tree; record the `ran X of Y tests (phases ...)` lines.
- [ ] **End-of-phase review (CLAUDE.md, uncapped):** `/code-review high --max-findings all` over the whole phase's diff (Days 207-221 from the commit before Day 207 to the working tree); verify, fix or decline each finding; record the count and the outcome under "Post-implementation code review".
- [ ] Update `plans/phase-16-std-wrappers.md` (status complete), `PLAN.md` (roadmap row, log entry), `STATUS.md` (next: Phase 17 needs its day files before it starts), `KNOWN-ISSUES.md` sweep; `CURRENT_PHASE` stays 16 until Phase 17's first day starts; commit and push.
- [ ] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** the standing policy is in `CLAUDE.md`, `PLAN.md` and memory, the closing lines are recorded with every test passing on both machines, the benchmarks are within noise (or filed), the gate is green, and Phase 16 is marked complete.
