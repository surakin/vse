# Phase 26 / Day 285 — The `os` Backend, Console and Log; Base Tests Pass

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 284.

Implement `engine/foundation/os/os.h`, `command_line.h`, `session.h` and the GPU driver probe for macOS, share what is identical with Linux in
`os/posix/`, and run the phase 0 suite on macOS. `scripts/test-phase.sh` runs on macOS from Day 286, so today's runs are plain `ctest`, reported as such.

## Tasks

- [ ] **`os/posix/`** (per the spec's inventory): move the parts of `os/linux/os.cpp` that are the same POSIX calls on both systems (`Write`, `Read`,
      `Open`, `CreateFileForWriting`, `Close`, `WriteAllWithTimeout` with `poll`, `ReserveMemory`/`ReleaseMemory` with `mmap`/`munmap`,
      `LoadLibrary`/`GetSymbol`/`UnloadLibrary` with `dlopen`) into `os/posix/os_posix.cpp`; Linux keeps only what is Linux's. `modules.txt`,
      `platform_neutral_check` and the `file_access_check` allow list name the new path (the descriptor primitives move with their reason). Linux
      behavior and tests unchanged (file the Linux run).
- [ ] **`os/macos/os.cpp`:** `MonotonicTimeNanos` (`clock_gettime_nsec_np(CLOCK_UPTIME_RAW)` or `mach_absolute_time` with the timebase; the spec
      picks, and the test checks it never goes backwards across threads), `SleepNanos` (`nanosleep` resumed until the deadline; measure the shortest real
      sleep for 1 µs, 100 µs, 1 ms), `Exit`, `Abort`, `EnvironmentCount`/`EnvironmentEntry` through `*_NSGetEnviron()`, `LastError` (errno), signals
      through `sigaction` as on Linux (`os::SetSignalHandler`), `MSG_NOSIGNAL`'s absence handled (`SO_NOSIGPIPE` or `SIGPIPE` blocked for the write,
      whichever the shared code does), `O_CLOEXEC` everywhere.
- [ ] **Memory:** `ReserveMemory` returns zeroed pages (`MAP_ANON | MAP_PRIVATE`); page size from `getpagesize()` (4 KiB on x86-64, 16 KiB on
      Apple silicon: the code must not assume 4 KiB; a test checks alignment to the reported size).
- [ ] **`os/macos/session.mm`** per `engine/foundation/os/macos/SESSION.md` (`CGSessionCopyCurrentDictionary`, `CGSSessionScreenIsLocked`,
      `kCGSessionOnConsoleKey`, idle through `CGEventSourceSecondsSinceLastEventType`, `CGDisplayIsAsleep`, `Unknown` without a window server) through
      `SessionStateFromFlags`; `tests/foundation/session_state_test.cpp` runs unchanged; check it over SSH (no window server: `Unknown`).
- [ ] **Command line:** the neutral `os/command_line.cpp` is used as is (macOS `argv` is UTF-8); a child-process test passes non-ASCII text, quotes and
      backslashes, as on Windows.
- [ ] **GPU driver probe:** `os::VulkanLoaderName()` gets an `os/macos/vulkan_loader_name.cpp` that answers "none" (decision 8), and the probe reports
      "not available" in the way its callers already handle; `gpu_driver_loading_test.cpp` skips with that reason.
- [ ] **Console and log:** the log writes UTF-8 to standard output through `os::Write`; a child writes a non-ASCII line through the log to a pipe and the
      parent reads the same bytes; Terminal.app shows it (checked by hand).
- [ ] **Base-layer tests on macOS:** `tests/foundation` (`foundation_base_tests`), `tests/types` and `tests/foundation_sanitized` build and run; tests of
      higher layers arrive with their layer (file layer Days 287-288, debugging 274-276, settings 277, window 279); record per file. ASan and TSan of the
      sanitized threading tests pass; record any false positive with its suppression and reason.
- [ ] **Documentation:** `os.h`, `session.h` comments name the macOS behavior where it differs (the clock, the page size, session over SSH);
      `docs/guide/platform-os.md` gets a "macOS" section; `docgen lint` zero; reference regenerated (on Linux until Day 286).
- [ ] **Runs:** macOS: `ctest -L phase0`, `-L phase10`, `-L phase16`, `-L phase26` from `build-macos` (plain, asan and tsan executables), quoted as plain
      ctest results, unity on and off. Linux: file `scripts/test-phase.sh --affected` (the `posix/` move touches shared files) plus asan and tsan.
- [ ] **End-of-day review:** `/code-review high`. Extra scrutiny: the descriptor path used by the crash report (lock-free, no allocation), the timebase
      conversion's overflow, `SleepNanos` never returning early, `dlopen`'s search path (no current directory), the environment pointer's lifetime.
- [ ] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push.

**Done:** `os.h`, `session.h` and the probe are implemented for macOS with the POSIX part shared; the phase 0, 10, 16 and 26 tests pass on macOS in
three flavors and both unity modes (quoted); `platform-os.md` has a macOS section; the Linux run of the moved files is filed.
