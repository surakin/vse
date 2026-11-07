# Phase 13 / Day 149 — The `os` Backend, Console and Log; Foundation Tests Pass

**Phase:** [Phase 13 — Windows Port + DX12 Backend](../phase-13-windows-dx12.md)
**Depends on:** Day 148.

Implement `engine/foundation/os/os.h`, `command_line.h` and `session.h` for Windows, port the log's output path, and run the phase 0 suite on Windows for
the first time. `scripts/test-phase.sh` does not run on Windows until Day 150, so today's Windows runs are plain `ctest` invocations, reported as such.

## Tasks

- [x] **UTF-16 boundary helpers** (`os/windows/utf16.h/.cpp`, internal): UTF-8 → UTF-16 into a caller buffer or an engine `String`-like wide buffer, and
      back, with invalid input reported (unpaired surrogates from Windows become U+FFFD and are counted, never dropped silently); a long-path helper that
      adds `\\?\` for absolute paths over `MAX_PATH`. Tests (phase 13): round trips of every Unicode plane, unpaired surrogates, empty and maximum lengths.
- [x] **`os/windows/os.cpp`:** `Exit` (`ExitProcess` after the log flush the Linux path does), `Write`/`Read`/`Open`/`CreateFileForWriting`/`Close` per the
      Day 147 descriptor decision (flags translated from the POSIX names the callers pass; `kMaxPathBytes` honored), `WriteAllWithTimeout` (a pipe whose
      reader stopped: overlapped write with a deadline or a non-blocking pipe mode; the crash path's constraints from the spec), `ReserveMemory`/`ReleaseMemory`
      (`VirtualAlloc` reserve+commit, zeroed; `VirtualFree` with `MEM_RELEASE`, so the size argument is checked, not used), `MonotonicTimeNanos`
      (`QueryPerformanceCounter` with an overflow-free conversion), `SleepNanos` (high-resolution waitable timer `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`,
      resumed until the deadline; measured: the shortest real sleep for 1 µs, 100 µs, 1 ms), `LoadLibrary`/`GetSymbol`/`UnloadLibrary` (`LoadLibraryExW` with
      `LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`, so the current directory is never searched), `EnvironmentCount`/`EnvironmentEntry` (`GetEnvironmentStringsW`
      converted to UTF-8 `NAME=value`, skipping the `=C:`-style drive entries, documented).
- [x] **`os/windows/command_line.cpp`:** arguments from `GetCommandLineW` split with `CommandLineToArgvW`, converted to UTF-8, so `main`'s narrow `argv`
      (which uses the ANSI code page unless the manifest's UTF-8 setting took effect) is never trusted for non-ASCII text; test with a child process whose
      arguments contain non-ASCII text, quotes and backslashes.
- [x] **`os/windows/session.cpp`** per `engine/foundation/os/windows/SESSION.md` (input desktop name, `WTSQuerySessionInformation`, `SessionStateFromFlags`);
      `tests/foundation/session_state_test.cpp` runs unchanged.
- [x] **Console and log:** `log/log.cpp` loses its POSIX includes (the descriptor work goes through `os::`); the console gets UTF-8 (`SetConsoleOutputCP(CP_UTF8)`)
      and virtual-terminal colors (`ENABLE_VIRTUAL_TERMINAL_PROCESSING`, falling back to no color when it fails or output is redirected); `isatty`'s
      counterpart is `GetFileType == FILE_TYPE_CHAR`. A test writes a non-ASCII log line to a pipe and reads back the same bytes.
- [x] **`gpu_driver_probe`:** the Windows answer (what the Linux probe reports, from `EnumDisplayDevices` or DXGI; whichever the spec chose) or a documented
      "not available" result the callers already handle.
- [x] **Base-layer tests on Windows:** every test that links only `foundation_base` (Phase 12 made these explicit: `tests/types` and the core `tests/foundation`
      files) builds and runs; tests of higher layers arrive with their layer (file layer Days 151-152, debugging Days 154-156, settings Day 157, window Day 158)
      and are not configured on Windows until then; record per file. (The `flavors 13` line arrived on Day 148.)
- [x] **ASan on Windows:** `foundation_base_asan` and the sanitized threading tests of `tests/foundation_sanitized` build and pass with `/fsanitize=address`; record
      any MSVC ASan false positive with its suppression and reason.
- [x] **Documentation:** `os.h`, `command_line.h`, `session.h` comments name the Windows behavior where it differs (the environment's drive entries, the
      `ReleaseMemory` size, the sleep resolution); `docs/guide/platform-os.md` gets a "Windows" section; `docgen lint` zero; reference regenerated (on Linux
      until Day 150).
- [ ] **Runs:** Windows: `ctest -L phase0` and `-L phase13` from the build directory (plain and the asan executables), quoted as plain ctest results;
      `clang-cl` build of the same set. Linux: `scripts/test-phase.sh --affected` (shared `log.cpp` and headers changed) and asan/tsan; quote the lines.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: the descriptor path used by the crash report (lock-free, no allocation), the UTF-16 conversion
      bounds, `SleepNanos` never returning early, `LoadLibrary`'s search order.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** `os.h`, `command_line.h` and `session.h` are implemented for Windows, the log writes UTF-8 to a Windows console and pipe, the phase 0 suite passes on
Windows in plain and ASan, and Linux still passes.

## Result (2026-11-07, on the Windows PC)

- **`os/windows/utf16.h/.cpp`:** UTF-8 to UTF-16 and back into caller buffers (full length returned, whole code points only, always terminated),
  invalid input counted and replaced by U+FFFD per maximal subpart (after the review), no lock and no allocation; `PathToWin32` turns `/` into `\`,
  and from 248 UTF-16 units (CreateDirectoryW's limit) adds `\\?\` or `\\?\UNC\` and resolves `.`, `..` and trailing dots and spaces as Win32 would
  without the prefix. No Windows header in it.
- **`os/windows/os.cpp`:** descriptors are an engine table (0-2 the standard handles, 256 slots claimed with a compare-and-swap), written with
  `WriteFile` and, for a console, converted to UTF-16 and written with `WriteConsoleW` (the console's code page is never changed); `Read` from a
  console gives UTF-8 through `ReadConsoleW`; a closed pipe reads as end of file. `WriteAllWithTimeout` runs its writes on a helper thread from a
  64 KiB static buffer and waits only until the deadline (after the review; the first version switched the shared pipe to `PIPE_NOWAIT`). `Exit` is
  `std::exit` (as Linux's `::exit`: atexit handlers run), not `ExitProcess`. `ReserveMemory` is `VirtualAlloc` reserve+commit; `ReleaseMemory`
  releases the whole reservation. `MonotonicTimeNanos` is `QueryPerformanceCounter` (seconds and remainder separately). `SleepNanos` uses one
  high-resolution waitable timer per thread, re-armed until the deadline. `LoadLibrary` is `LoadLibraryExW` with the search flags: never the current
  directory, a relative path with a directory refused. The environment comes from `GetEnvironmentStringsW`, without the `=C:` drive entries.
- **Measured `SleepNanos`** (50 runs each): 1 µs requested, 3.5 µs shortest and 557 µs longest; 100 µs, 510 µs shortest; 1 ms, 1.02-2.17 ms. Never early.
- **Command line, changed from the plan:** not rebuilt from `GetCommandLineW`. `command_line_test.cpp` stashes a fake `argv` and expects it back, and
  the manifest (Day 148) already makes the process code page UTF-8, from which the C runtime builds `argv`. So `os/linux/command_line.cpp` became the
  neutral `os/command_line.cpp` for both platforms; a child-process test passes non-ASCII text, quotes and backslashes and checks the UTF-8 bytes.
- **Session state** (`os/windows/session.cpp`): `WTSQuerySessionInformation` (inactive) and the input desktop's name (locked), through
  `SessionStateFromFlags`; Windows has no idle hint. `session_state_test.cpp` passes unchanged.
- **Console and log:** `log/log.cpp` lost its unused `<fcntl.h>`. The log has no colors, so the plan's virtual-terminal task had nothing to apply to.
  A child writes a non-ASCII line through the log to a pipe and the parent reads the same bytes; in a console the line (`é ö ✓ 😀`) showed correctly
  (checked by hand). The log writes to standard output, not standard error (the first version of the test assumed the latter).
- **GPU driver probe:** `os::VulkanLoaderName()` (`libvulkan.so.1`, `vulkan-1.dll`; per-platform files `os/<platform>/vulkan_loader_name.cpp`) is the one
  place the name is chosen; the probe and `gpu_driver_loading_test.cpp` use it, and Day 160's Vulkan loader will.
- **Base-layer tests on Windows:** `tests/foundation` (`foundation_base_tests`), `tests/types` and `tests/foundation_sanitized` join the Windows build;
  `foundation_tests` (file and window tests) waits for the platform library. Four shared fixes were needed: `int128_test.cpp` compares `<=>` results
  in double parentheses (MSVC rejects Catch2's decomposition of `x < 0` on an ordering, C7595); `os_timing_test.cpp` sets the environment through the
  new `tests/support/test_env.h` (POSIX `setenv` does not exist on Windows); `types_tests_portable` is built only where a native `__int128` exists
  (with MSVC the library is the portable path and the executable defined every symbol twice); `windows.h`'s `IN`, `OUT`, `OPTIONAL`, `ERROR`,
  `DELETE` left the wrapper's list (later SDK headers such as `wtsapi32.h` use them).
- **ASan on Windows:** `/fsanitize=address` with `_DISABLE_STL_ANNOTATION` (Catch2 is not ASan-built, and the standard library refuses to link
  annotated and unannotated objects, LNK2038); the ASan runtime DLLs are copied beside each ASan test executable (`engine_asan_runtime`). No ASan report.
- **Runs (Windows; plain `ctest`, since `scripts/test-phase.sh` runs on Windows from Day 150):** `ctest --preset windows-msvc`: 190 of 190 passed
  (phase 0: 91 plain + 1 asan; phase 1: 8; phase 10: 25 + 23; phase 13: 19 + 14 at the first count, plus the 4 review tests); `ctest --preset
  windows-clang-cl`: 181 of 181. No warning with either compiler, unity on and off.
- **For the Linux run at the phase close:** shared files changed today: `os/os.h`, `os/session.h`, `os/command_line.h` (comments),
  `os/gpu_driver_probe.h/.cpp` (`VulkanLoaderName`), `log/log.cpp`, `os/linux/command_line.cpp` moved to `os/command_line.cpp`, the new
  `os/linux/vulkan_loader_name.cpp`, `engine/foundation/CMakeLists.txt`, `cmake/EngineCompilerOptions.cmake`, `tests/foundation/CMakeLists.txt`,
  `tests/types/CMakeLists.txt`, `tests/foundation_sanitized/CMakeLists.txt`, `tests/foundation/os_timing_test.cpp`, `gpu_driver_loading_test.cpp`,
  `tests/types/int128_test.cpp`, `tests/support/test_env.h`, `tools/module_deps_check/modules.txt`; reference regeneration for the four headers;
  guide `platform-os.md`.

## Post-implementation code review

`/code-review high` over the day's diff: **10 findings, 7 fixed, 1 deferred, 2 declined.**

Fixed: (1) a console paused by a selection would block `WriteAllWithTimeout` past its deadline and (4) the `PIPE_NOWAIT` switch changed the shared
standard-error pipe for every other thread: both replaced by the helper-thread writer, with a test that a second call gives up at once while the first is
still blocked; (2) the long-path prefix now starts at 248 units (directory limit) and (3) prefixed paths are normalized, with tests for both; (7) the UTF-8
decoder now replaces per maximal subpart (`ED A0 80` is three U+FFFD), tests updated; (9) the console check is cached per standard handle; (10) a console
`Read` returns UTF-8. Deferred to Day 150 (task added): (5) `file_access_check` will need allow-list entries for `os/windows/os.cpp` and this test once it
knows the Win32 file calls. Declined: (6) `ReserveMemory` commits eagerly because the interface promises memory that is usable at once (Linux's
private writable mapping is charged under strict overcommit too), and committed pages cost no physical memory until touched; (8) `_DISABLE_STL_ANNOTATION`
loses container-overflow checks only in test code with standard containers, which Linux ASan still covers; an ASan Catch2 build is not worth it now.
