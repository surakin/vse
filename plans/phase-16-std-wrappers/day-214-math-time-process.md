# Phase 16 / Day 214 — Scalar Math, Time, Process, Signals, Console

**Phase:** [Phase 16 — Standard-Library Wrappers](../phase-16-std-wrappers.md)
**Depends on:** Days 208, 211.

Wrap the remaining C++ and C library categories: `<cmath>`, `<chrono>`/`<ctime>`, `<cstdlib>` process functions, `<csignal>`, `<cerrno>`, `<exception>`, `<cstdio>` console output.

## Tasks

- [x] `lang/scalar.h` (or `math/scalar.h`, decided by the Day 207 measurement): `Sqrt`, `Sin`, `Cos`, `Tan`, `Asin`, `Acos`, `Atan2`, `Pow`, `Exp`, `Log`, `Floor`, `Ceil`, `Round`, `LRound`, `Fmod`, `Abs`, `FMin`, `FMax`, `IsFinite`, `IsNaN`, `IsInf`, `CopySign`, `f32` and `f64` overloads; `constexpr` only where the standard's are; fast-math flags unchanged.
- [x] Time: decide from the 65 `std::chrono` uses whether `Duration`/`TimePoint` value types over `os::` clocks are needed or `os::Now()`/`os::Sleep()` suffice; implement the decision in `os`, port `sync`'s timed waits to it.
- [x] `os::Print`, `os::PrintError`, `os::Flush` replace `printf`, `fprintf(stderr…)`, `fputs`, `fflush` on the console (formatting through `FormatTo`; the `file_access_check` allow list entry for console output moves with it).
- [x] `os::Abort`, `os::Exit`, `os::GetEnvironment` (extend the existing), `os::LastError` (`errno`), `os::InstallSignalHandler`/`RestoreSignalHandler` (the debug module's `<csignal>` use), `<exception>` (`std::terminate`) uses: wrap in `os`, mark `Signal-safe:` where true.
- [x] Tests: math against `<cmath>` over sampled inputs including NaN, infinities and subnormals; time monotonicity and sleep bounds; print to a captured descriptor; signal install/restore; environment round trip.
- [x] Port every `std::` use of these categories in `engine/`, `game/`, `tools/` (non-Windows files), including `config_access_check` allow-list lines that name `std::getenv` (the settings registry stays the only reader through `os::GetEnvironment`).
- [x] Docs, modules.txt, `ARCHITECTURE.md`, guide pages `foundation-math.md` and `foundation-os.md` updated.
- [x] **Day review (CLAUDE.md, required):** scoped tests (`scripts/test-phase.sh --affected`, plain, then asan and tsan where the day touched concurrency or memory code), then `/code-review high` on the day's diff; verify, fix or decline each finding with a reason and record the count and outcome under "Post-implementation code review"; `STATUS.md` and `KNOWN-ISSUES.md` updated; commit and push.

**Done:** no `<cmath>`, `<chrono>`, `<ctime>`, `<csignal>`, `<cerrno>`, `<exception>`, `<cstdio>`, `<cstdlib>` include or `std::` use remains outside the wrapper modules and the Windows-only files; `file_access_check` and `config_access_check` pass; tests pass plain and asan.

## Result

- `lang/scalar.h` (math), `os/console.h` (`Print*`, `Flush`, `Abort`, `ExitNow`, `GetEnvironment`; `os::Exit` already existed in `os/os.h`), `os/last_error.h` (`LastError`, `SetLastError`), `os/signal.h` (`SetSignalHandler`), `cstr/number.h` (`ParseDoubleExact`, `FormatShortest`). Time: `os::MonotonicTimeNanos()` and `os::SleepNanos()` already existed and suffice (the only `std::chrono` use outside `sync` was `settings.cpp`'s `NowSeconds`, now on them); no `Duration` type was added. `<ctime>` became the POSIX `<time.h>` in the two `os` files that call `clock_gettime`. Gates: `config_access_check` matches `GetEnvironment(`, `file_access_check` lists the console test's `mkstemp`, `std_container_check` lost three stale entries. Cross-syntax shims gained `cstdarg`, `cwchar`, `cstring`/`cstdlib`/`cstdio` extensions. After the day: no `std::` and no standard include remain in `engine/`, `game/`, `tools/` outside the wrapper modules, `jobs` coroutines (Day 215), the Windows-only files and `reflect_json.cpp`.
- Scoped runs: plain `ran 1968 of 5051 tests (phases: all; flavors: plain; display: included; crash tests: excluded)`, asan `ran 1452 of 5051 tests (phases: all; flavors: asan; display: included; crash tests: excluded)`, tsan `ran 1134 of 5051 tests (phases: all; flavors: tsan; display: included; crash tests: excluded)`; the only failures are the two known X11 window tests (locked session, `KNOWN-ISSUES.md` Day 187). Builds clean with Clang, GCC, unity and `-DENGINE_UNITY_BUILD=OFF`. `ctest -L docs` 7 of 7.

## Post-implementation code review (Days 211-214, `/code-review high`)

6 findings. Fixed: `os::GetEnvironmentVariable` collides with the Win32 `GetEnvironmentVariable` macro (renamed `os::GetEnvironment`); a dead initializer in `settings/desc.cpp`. Declined with reason: `WFormatTo`/`ScanFrom` have no format attribute (templates; the guide now says so); `ParseDoubleExact` follows `std::from_chars` (documented and tested); the `console.h` allowlist entry (narrowed by the Day 220 gate); the port script's skip list (a one-shot tool, removed at the phase close).
