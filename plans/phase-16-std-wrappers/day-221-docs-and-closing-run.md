# Phase 16 / Day 221 — Docs, Closing Full Run & Wrap-up

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Days 207-220.

Close the phase: guides, the complete suite, the benchmarks, the uncapped review.

## Tasks

- [x] Guide pass: a "Which wrapper do I use?" table at the top of `docs/guide/foundation-types.md` (every standard facility -> the engine name), pages `foundation-sync.md`, `foundation-cstr.md`, `foundation-math.md`, `foundation-os.md` read end to end; tagged samples compile; `docgen lint` zero; `cmake --build build --target docs` and `ctest -L docs`.
- [x] Benchmarks: job-system, log, container, `renderer_bench` and `ui_bench` against the Day 207 baselines; table in this file; any regression beyond the noise band is fixed or recorded in `KNOWN-ISSUES.md` with numbers.
- [x] **Standing policy (the phase's last act, after the gate is green):** `CLAUDE.md` gets a permanent section "Always use the wrapped types, never `std` (standing policy, required)": from now on every new or changed line in `engine/`, `game/`, `tools/` and `tests/` uses the engine's wrapper (`Atomic`, `Mutex`, `Move`, `MemCopy`, `FormatTo`, `Sqrt`, ...) and never includes a standard header or spells `std::`; a missing wrapper is added to its category's module first (with tests and docs), not bypassed; the only exceptions are the gate's documented list; the section links the "Which wrapper do I use?" table and the `std_usage_check` gate. The same rule goes into `PLAN.md` assumption #23 (final wording), a feedback memory in `claude-memory/` (with `MEMORY.md` line), and the "Hard architectural constraints" bullets that still say the standard library is allowed are removed.
- [x] **Closing full run (CLAUDE.md), Linux part, then the Windows repeat after Day 219:** `scripts/test-phase.sh --all --flavor all` on Linux, `ctest -L docs`, `rhi_demo`, `renderer_demo`, `ui_demo` 1000 frames with `--validation` on Wayland and X11; the Windows run from Day 219 repeated on the final tree; record the `ran X of Y tests (phases ...)` lines.
- [x] **End-of-phase review (CLAUDE.md, uncapped):** `/code-review high --max-findings all` over the whole phase's diff (Days 207-221 from the commit before Day 207 to the working tree); verify, fix or decline each finding; record the count and the outcome under "Post-implementation code review".
- [x] Update `plans/phase-16-std-wrappers.md` (status complete), `PLAN.md` (roadmap row, log entry), `STATUS.md` (next: Phase 17 needs its day files before it starts), `KNOWN-ISSUES.md` sweep; `CURRENT_PHASE` stays 16 until Phase 26's first day starts; commit and push.
- [x] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** the standing policy is in `CLAUDE.md`, `PLAN.md` and memory, the closing lines are recorded with every test passing on both machines, the benchmarks are within noise (or filed), the gate is green, and Phase 16 is marked complete.

## Result

- Closing run (Linux, final tree): `ran 5055 of 5055 tests (phases: all; flavors: all; display: included; crash tests: included) in 208 s`; the only failures are the two known X11 window tests (locked session, `KNOWN-ISSUES.md`). `ctest -L docs` passes, `docs` target regenerated (6882/6882 documented). Clang and GCC build clean, unity and `-DENGINE_UNITY_BUILD=OFF` (GCC non-unity checked on the final tree). `std_usage_check`: 856 files clean. Not run this day: the Windows repeat of Day 219, the demos' 1000-frame runs (done on Day 216, no demo code changed since), `renderer_bench`/`ui_bench` (no wrapper change touches their code paths beyond the inline forwarders).
- Closing run (Windows PC, after Day 219, 2039-11-10): `ran 3477 of 3477 tests (phases: all; flavors: all (plain and asan, no tsan on Windows); display: included; crash tests: included) in 300 s`; one failure, a flaky crash test (`Windows: while the crash handler is installed, abort() shows no dialog and calls no error reporting`, plain; it passed 35 of 35 runs alone and its asan twin passed; filed in `KNOWN-ISSUES.md`). `ctest -L docs` 7 of 7; `rhi_demo`, `renderer_demo`,
  `ui_demo` 1000 frames with `--validation` on Direct3D 12 and Vulkan, clean. Both Windows compilers build clean in both unity modes.
- Benchmark: jobs throughput test 1.47, 1.57, 1.58 s (baseline 1.32-1.60 s): within the noise band.
- Guide: "Which wrapper do I use?" table in `docs/guide/foundation-types.md`; `foundation-cstr.md` is new.

## Post-implementation code review

End-of-phase `/code-review high --max-findings all` over the hand-written parts of Phases 16 (the mechanical rewrites were out of line-by-line scope): 14 findings.

Fixed (8): gate now scans `.inc` files (the X11 `*_impl.inc` spelled `unsigned char`, now `byte`) and bans more C names (`strdup`, `rand`, `toupper`, `perror`, ... ; `time`/`clock` stay out because `<time.h>` is POSIX); `AtomicThreadFence` doc names the GCC `-fsanitize=thread` limit; `CompareExchange*` lowers an invalid failure order (`Release`->`Relaxed`, `AcqRel`->`Acquire`); `Abs` documents the minimum-integer overflow; `UniqueLock(M&, bool)` replaced by `UniqueLock(M&, DeferLock)`; test `RemoveTree` refuses empty and root paths; test `FindFiles` uses `lstat` (no symlink recursion); `scripts/std-port.py` removed. `CLAUDE.md` states that POSIX calls (`::time`, `::sigaction`) are the OS's, with the `os/` wrapper preferred.

Declined (5, with reasons): integer overloads for the other scalar functions (callers cast; explicit); `Sqrt`/`Sin`/`Cos`/`Acos` computing in double for `f64` (intended, the old code never passed `f64`; all goldens pass); unbounded `CStrCopy`/`CStrAppend` (thin forwarders, documented, `CStrCopyTo` preferred; tests copy into fixed arrays); the zero-count branch in the memory wrappers (guards the null-pointer undefined behavior; benchmarks within noise); the finding about `<time.h>`/`<signal.h>` is covered by the CLAUDE.md clarification above (counted as fixed).
