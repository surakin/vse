# Phase 12 / Day 142 — The `os` Backend, Console and Log; Foundation Tests Pass

**Phase:** [Phase 12 — Windows Port + DX12 Backend](../phase-12-windows-dx12.md)
**Depends on:** Day 141.

Implement `engine/foundation/os/os.h`, `command_line.h` and `session.h` for Windows, port the log's output path, and run the phase 0 suite on Windows for
the first time. `scripts/test-phase.sh` does not run on Windows until Day 143, so today's Windows runs are plain `ctest` invocations, reported as such.

## Tasks

- [ ] **UTF-16 boundary helpers** (`os/windows/utf16.h/.cpp`, internal): UTF-8 → UTF-16 into a caller buffer or an engine `String`-like wide buffer, and
      back, with invalid input reported (unpaired surrogates from Windows become U+FFFD and are counted, never dropped silently); a long-path helper that
      adds `\\?\` for absolute paths over `MAX_PATH`. Tests (phase 12): round trips of every Unicode plane, unpaired surrogates, empty and maximum lengths.
- [ ] **`os/windows/os.cpp`:** `Exit` (`ExitProcess` after the log flush the Linux path does), `Write`/`Read`/`Open`/`CreateFileForWriting`/`Close` per the
      Day 140 descriptor decision (flags translated from the POSIX names the callers pass; `kMaxPathBytes` honored), `WriteAllWithTimeout` (a pipe whose
      reader stopped: overlapped write with a deadline or a non-blocking pipe mode; the crash path's constraints from the spec), `ReserveMemory`/`ReleaseMemory`
      (`VirtualAlloc` reserve+commit, zeroed; `VirtualFree` with `MEM_RELEASE`, so the size argument is checked, not used), `MonotonicTimeNanos`
      (`QueryPerformanceCounter` with an overflow-free conversion), `SleepNanos` (high-resolution waitable timer `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`,
      resumed until the deadline; measured: the shortest real sleep for 1 µs, 100 µs, 1 ms), `LoadLibrary`/`GetSymbol`/`UnloadLibrary` (`LoadLibraryExW` with
      `LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`, so the current directory is never searched), `EnvironmentCount`/`EnvironmentEntry` (`GetEnvironmentStringsW`
      converted to UTF-8 `NAME=value`, skipping the `=C:`-style drive entries, documented).
- [ ] **`os/windows/command_line.cpp`:** arguments from `GetCommandLineW` split with `CommandLineToArgvW`, converted to UTF-8, so `main`'s narrow `argv`
      (which uses the ANSI code page unless the manifest's UTF-8 setting took effect) is never trusted for non-ASCII text; test with a child process whose
      arguments contain non-ASCII text, quotes and backslashes.
- [ ] **`os/windows/session.cpp`** per `engine/foundation/os/windows/SESSION.md` (input desktop name, `WTSQuerySessionInformation`, `SessionStateFromFlags`);
      `tests/foundation/session_state_test.cpp` runs unchanged.
- [ ] **Console and log:** `log/log.cpp` loses its POSIX includes (the descriptor work goes through `os::`); the console gets UTF-8 (`SetConsoleOutputCP(CP_UTF8)`)
      and virtual-terminal colors (`ENABLE_VIRTUAL_TERMINAL_PROCESSING`, falling back to no color when it fails or output is redirected); `isatty`'s
      counterpart is `GetFileType == FILE_TYPE_CHAR`. A test writes a non-ASCII log line to a pipe and reads back the same bytes.
- [ ] **`gpu_driver_probe`:** the Windows answer (what the Linux probe reports, from `EnumDisplayDevices` or DXGI; whichever the spec chose) or a documented
      "not available" result the callers already handle.
- [ ] **Foundation tests on Windows:** every `tests/foundation` and `tests/types` test file that is not `_linux` builds and runs; the ones that cannot yet (window
      tests: Day 151) are excluded from the Windows build by the platform rule, not deleted; record per file. Add the `flavors 12 plain,asan` line with the
      first phase-12 test.
- [ ] **ASan on Windows:** `foundation_asan` and the sanitized threading tests of `tests/foundation_sanitized` build and pass with `/fsanitize=address`; record
      any MSVC ASan false positive with its suppression and reason.
- [ ] **Documentation:** `os.h`, `command_line.h`, `session.h` comments name the Windows behavior where it differs (the environment's drive entries, the
      `ReleaseMemory` size, the sleep resolution); `docs/guide/platform-os.md` gets a "Windows" section; `docgen lint` zero; reference regenerated (on Linux
      until Day 143).
- [ ] **Runs:** Windows: `ctest -L phase0` and `-L phase12` from the build directory (plain and the asan executables), quoted as plain ctest results;
      `clang-cl` build of the same set. Linux: `scripts/test-phase.sh --affected` (shared `log.cpp` and headers changed) and asan/tsan; quote the lines.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: the descriptor path used by the crash report (lock-free, no allocation), the UTF-16 conversion
      bounds, `SleepNanos` never returning early, `LoadLibrary`'s search order.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** `os.h`, `command_line.h` and `session.h` are implemented for Windows, the log writes UTF-8 to a Windows console and pipe, the phase 0 suite passes on
Windows in plain and ASan, and Linux still passes.
