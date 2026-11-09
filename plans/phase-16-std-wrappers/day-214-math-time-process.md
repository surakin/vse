# Phase 16 / Day 214 — Scalar Math, Time, Process, Signals, Console

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Days 208, 211.

Wrap the remaining C++ and C library categories: `<cmath>`, `<chrono>`/`<ctime>`, `<cstdlib>` process functions, `<csignal>`, `<cerrno>`, `<exception>`, `<cstdio>` console output.

## Tasks

- [ ] `lang/scalar.h` (or `math/scalar.h`, decided by the Day 207 measurement): `Sqrt`, `Sin`, `Cos`, `Tan`, `Asin`, `Acos`, `Atan2`, `Pow`, `Exp`, `Log`, `Floor`, `Ceil`, `Round`, `LRound`, `Fmod`, `Abs`, `FMin`, `FMax`, `IsFinite`, `IsNaN`, `IsInf`, `CopySign`, `f32` and `f64` overloads; `constexpr` only where the standard's are; fast-math flags unchanged.
- [ ] Time: decide from the 65 `std::chrono` uses whether `Duration`/`TimePoint` value types over `os::` clocks are needed or `os::Now()`/`os::Sleep()` suffice; implement the decision in `os`, port `sync`'s timed waits to it.
- [ ] `os::Print`, `os::PrintError`, `os::Flush` replace `printf`, `fprintf(stderr…)`, `fputs`, `fflush` on the console (formatting through `FormatTo`; the `file_access_check` allow list entry for console output moves with it).
- [ ] `os::Abort`, `os::Exit`, `os::GetEnvironment` (extend the existing), `os::LastError` (`errno`), `os::InstallSignalHandler`/`RestoreSignalHandler` (the debug module's `<csignal>` use), `<exception>` (`std::terminate`) uses: wrap in `os`, mark `Signal-safe:` where true.
- [ ] Tests: math against `<cmath>` over sampled inputs including NaN, infinities and subnormals; time monotonicity and sleep bounds; print to a captured descriptor; signal install/restore; environment round trip.
- [ ] Port every `std::` use of these categories in `engine/`, `game/`, `tools/` (non-Windows files), including `config_access_check` allow-list lines that name `std::getenv` (the settings registry stays the only reader through `os::GetEnvironment`).
- [ ] Docs, modules.txt, `ARCHITECTURE.md`, guide pages `foundation-math.md` and `foundation-os.md` updated.
- [ ] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** no `<cmath>`, `<chrono>`, `<ctime>`, `<csignal>`, `<cerrno>`, `<exception>`, `<cstdio>`, `<cstdlib>` include or `std::` use remains outside the wrapper modules and the Windows-only files; `file_access_check` and `config_access_check` pass; tests pass plain and asan.
