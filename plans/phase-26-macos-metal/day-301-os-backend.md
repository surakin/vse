# Phase 26 / Day 301 — The `os` Backend, Console and Log; Base Tests Pass

**Phase:** [Phase 26 — macOS Port + Metal Backend](../phase-26-macos-metal.md)
**Depends on:** Day 300.

Implement `engine/foundation/os/os.h`, `command_line.h`, `session.h` and the GPU driver probe for macOS, share what is identical with Linux in
`os/posix/`, and run the phase 0 suite on macOS. `scripts/test-phase.sh` runs on macOS from Day 302, so today's runs are plain `ctest`, reported as such.

## Tasks

- [x] **`os/posix/`** (per the spec's inventory): move the parts of `os/linux/os.cpp` that are the same POSIX calls on both systems (`Write`, `Read`,
      `Open`, `CreateFileForWriting`, `Close`, `WriteAllWithTimeout` with `poll`, `ReserveMemory`/`ReleaseMemory` with `mmap`/`munmap`,
      `LoadLibrary`/`GetSymbol`/`UnloadLibrary` with `dlopen`) into `os/posix/os_posix.cpp`; Linux keeps only what is Linux's. `modules.txt`,
      `platform_neutral_check` and the `file_access_check` allow list name the new path (the descriptor primitives move with their reason). Linux
      behavior and tests unchanged (file the Linux run).
- [x] **`os/macos/os.cpp`:** `MonotonicTimeNanos` (`clock_gettime_nsec_np(CLOCK_UPTIME_RAW)` or `mach_absolute_time` with the timebase; the spec
      picks, and the test checks it never goes backwards across threads), `SleepNanos` (`nanosleep` resumed until the deadline; measure the shortest real
      sleep for 1 µs, 100 µs, 1 ms), `Exit`, `Abort`, `EnvironmentCount`/`EnvironmentEntry` through `*_NSGetEnviron()`, `LastError` (errno), signals
      through `sigaction` as on Linux (`os::SetSignalHandler`), `MSG_NOSIGNAL`'s absence handled (`SO_NOSIGPIPE` or `SIGPIPE` blocked for the write,
      whichever the shared code does), `O_CLOEXEC` everywhere.
- [x] **Memory:** `ReserveMemory` returns zeroed pages (`MAP_ANON | MAP_PRIVATE`); page size from `getpagesize()` (4 KiB on x86-64, 16 KiB on
      Apple silicon: the code must not assume 4 KiB; a test checks alignment to the reported size).
- [x] **`os/macos/session.mm`** per `engine/foundation/os/macos/SESSION.md` (`CGSessionCopyCurrentDictionary`, `CGSSessionScreenIsLocked`,
      `kCGSessionOnConsoleKey`, idle through `CGEventSourceSecondsSinceLastEventType`, `CGDisplayIsAsleep`, `Unknown` without a window server) through
      `SessionStateFromFlags`; `tests/foundation/session_state_test.cpp` runs unchanged; check it over SSH (no window server: `Unknown`).
- [x] **Command line:** the neutral `os/command_line.cpp` is used as is (macOS `argv` is UTF-8); a child-process test passes non-ASCII text, quotes and
      backslashes, as on Windows.
- [x] **GPU driver probe:** not built on macOS (amended on Day 299, done on Day 300); when `tests/foundation` joins the macOS build today, leave
      `gpu_driver_loading_test.cpp` out there (it includes the probe), and replace the temporary Linux `os.cpp` and `session.cpp` the base layer builds on
      macOS since Day 300 (`KNOWN-ISSUES.md`).
- [x] **Process figures:** `ResidentMemoryKiB` (`task_info(MACH_TASK_BASIC_INFO)`) and `OpenHandleCount` (`proc_pidinfo(PROC_PIDLISTFDS)`) in
      `os/macos/`: the Linux versions read `/proc` and compile on macOS but fail at run time (Day 299).
- [x] **Console and log:** the log writes UTF-8 to standard output through `os::Write`; a child writes a non-ASCII line through the log to a pipe and the
      parent reads the same bytes; Terminal.app shows it (checked by hand).
- [x] **Base-layer tests on macOS:** `tests/foundation` (`foundation_base_tests`), `tests/types` and `tests/foundation_sanitized` build and run; tests of
      higher layers arrive with their layer (file layer Days 303-304, debugging 274-276, settings 277, window 279); record per file. ASan and TSan of the
      sanitized threading tests pass; record any false positive with its suppression and reason.
- [x] **Documentation:** `os.h`, `session.h` comments name the macOS behavior where it differs (the clock, the page size, session over SSH);
      `docs/guide/platform-os.md` gets a "macOS" section; `docgen lint` zero; reference regenerated (on Linux until Day 302).
- [x] **Runs:** macOS: `ctest -L phase0`, `-L phase10`, `-L phase16`, `-L phase26` from `build-macos` (plain, asan and tsan executables), quoted as plain
      ctest results, unity on and off. Linux: file `scripts/test-phase.sh --affected` (the `posix/` move touches shared files) plus asan and tsan.
- [x] **End-of-day review:** `/code-review high`. Extra scrutiny: the descriptor path used by the crash report (lock-free, no allocation), the timebase
      conversion's overflow, `SleepNanos` never returning early, `dlopen`'s search path (no current directory), the environment pointer's lifetime.
- [x] Update `STATUS.md`, `KNOWN-ISSUES.md`; commit and push (ticked on Day 302; the Day 301 commit `2ddb6d28` did it).

**Done:** `os.h`, `session.h` and the probe are implemented for macOS with the POSIX part shared; the phase 0, 10, 16 and 26 tests pass on macOS in
three flavors and both unity modes (quoted); `platform-os.md` has a macOS section; the Linux run of the moved files is filed.

## Result (2026-11-11, the Mac)

- **The split.** `os/posix/os_posix.cpp` holds `Exit`, `Write`, `Read`, `WriteAllWithTimeout`, `Open`, `CreateFileForWriting`, `Close`,
  `ReserveMemory`/`ReleaseMemory`, `WallClockUnixMillis` (added by the main line on Day 254), `SleepNanos`, `GetSymbol`, `UnloadLibrary` and the
  environment functions; `os/posix/posix_os.h` declares the two hooks each platform supplies (`ToCPath`, shared, and `ProcessEnvironment`:
  `environ` on Linux, `*_NSGetEnviron()` on macOS). `os/linux/os.cpp` keeps `MonotonicTimeNanos`, `FillRandomBytes` (`getrandom`), the `/proc`
  process figures and `LoadLibrary`, unchanged. The main line's Day 254 had made the borrowed Linux file stop compiling on macOS (`getrandom`);
  the split ends that.
- **macOS's own** (`os/macos/os.cpp`): the clock is `clock_gettime_nsec_np(CLOCK_UPTIME_RAW)`, chosen over `mach_absolute_time` (it is
  nanoseconds already: no timebase multiplication, no overflow) and over macOS's `CLOCK_MONOTONIC` (which keeps counting through sleep, unlike
  Linux's). `FillRandomBytes` is `getentropy` in 256-byte calls. `ResidentMemoryKiB` is `task_info(MACH_TASK_BASIC_INFO)`; `OpenHandleCount` is
  `proc_pidinfo(PROC_PIDLISTFDS)` into pages of `ReserveMemory`. **`LoadLibrary` differs from Linux**: macOS's `dlopen` searches the current
  directory for a bare name, so the engine never passes one: an absolute path goes as named, a bare name is tried as `@executable_path/<name>`
  then `/usr/lib/<name>`, and a relative path with a directory is refused (the Windows rule). Signals stay `std::signal` (`os/signal.h`), as on
  Linux; nothing in the base layer uses `MSG_NOSIGNAL`, so there was nothing to replace.
- **Session** (`os/macos/session.mm`, ARC, CoreGraphics and Foundation linked on `foundation_base`): `Unknown` without a session dictionary,
  `Inactive` off the console, `Locked` from `CGSSessionScreenIsLocked`, `Idle` for a sleeping main display. The input-time idle route was left out
  (it needs a limit the engine has none of; Windows decided the same); `SESSION.md` updated. At the desk it answered `locked` (the Mac was
  locked). **Not checked over ssh**: `ssh localhost` needs a password here (`KNOWN-ISSUES.md`).
- **Found by the build: `Hash<usize>` did not exist on macOS.** `usize` is `unsigned long` there and `u64` `unsigned long long`, so
  `hash_map_test.cpp`'s `HashMap<usize, usize>` had no hash (on Linux the two are one type). `containers/hash_map.h` gained a `Hash<K>` constrained
  to `usize`/`isize` where they are distinct types; on Linux and Windows the constraint never holds.
- **Found while wiring the gates: `module_deps_check` did not scan `.mm` files**, so an Objective-C++ file could include an upper layer unseen.
  It scans them now (and `platform_neutral_check` rule 1 too; `_posix` test names are exempt like `_linux`).
- **Tests.** `tests/foundation` (`foundation_base_tests`, without `gpu_driver_loading_test.cpp`) and `tests/foundation_sanitized` join the macOS
  build. New `tests/macos/os_macos_test.cpp` in three flavors with its child `os_child_macos.cpp`: the clock across four threads (200,000 readings
  under one lock, none backwards), the shortest of 20 sleeps (1 µs: 5.1 µs; 100 µs: 106.9 µs; 1 ms: 1.058 ms; never early), zeroed pages aligned to
  `getpagesize()` (4096 here), the resident size growing by 64 MiB touched, the descriptor count following a `pipe`, `LoadLibrary` (absolute, bare
  name, refused relative forms, too long), the session query, and a child that reads `héllo wörld`, `a"b'c`, `back\slash\`, `日本` and an empty
  argument through `CommandLine::FromNative` and writes them and a UTF-8 log line through a pipe, byte for byte. Terminal.app was not looked at.
- **Runs (plain ctest, `scripts/test-phase.sh` arrives on Day 302):** `build-macos` (unity) `100% tests passed out of 379`; by label
  `-L phase0` 99 of 99, `-L phase10` 110 of 110, `-L phase16` 114 of 114, `-L phase26` 37 of 37 (each in plain, asan and tsan executables where
  the flavor exists). `build-macos-nounity` (`-DENGINE_UNITY_BUILD=OFF`) `100% tests passed out of 379`. No warnings in either. The gates by hand:
  `module_deps_check` 263 edges, 0 problems; `std_usage_check` 974 files clean; `file_access_check` 961 files clean; `config_access_check` 592
  files clean; `platform_neutral_check` clean. `docgen lint` and the reference are not built on the Mac before Day 302; the Linux run is filed.

## Post-implementation code review

`/code-review high` on the day's diff: 9 findings (two counted together in item 5). **6 fixed, 1 in part, 2 declined.**

1. **Fixed (a real bug, with a test shown to fail first): `WriteAllWithTimeout` wrote nothing to `/dev/null` on macOS.** macOS's `poll` answers
   `POLLNVAL` for devices it does not support (checked: `/dev/null` gives `0x20`, a pty gives `POLLOUT`), so a crash report to a stderr on
   `/dev/null` (launchd, `2>/dev/null`) came back 0 bytes. An open descriptor answering `POLLNVAL` is now written without the wait; a closed one is
   still a failure. `macOS: WriteAllWithTimeout writes to a device poll does not support` failed before the fix (1 of 3 assertions) and passes now.
2. **Fixed: a session dictionary without `kCGSessionOnConsoleKey` read as `Inactive`**, which would skip every window test; it is `Unknown` now
   (`session.h`: an unknown state never hides a failure), and non-`NSNumber` values are not trusted. Not testable without a forged dictionary.
3. **Fixed: the session test compared two queries,** which differ if the screen locks between them; it now checks that each is one of the states.
4. **Fixed: OS calls without `::`** (`posix_spawn_file_actions_*` in the test, the CoreGraphics calls in `session.mm`).
5. **Fixed: `HasSlash` duplicated `StringView::IndexOf`;** and **the `OpenHandleCount` comment** claimed Linux's version allocates nothing
   (`opendir` does): corrected.
6. **In part: `docs/reference/` not regenerated, `ctest -L docs` not run.** `docgen` is not built on the Mac before Day 302; the headers carry
   their `///` comments, and the regeneration is filed for the next Linux session (`KNOWN-ISSUES.md`), as on Day 300.
7. **Declined: one `Hash<K>` for every integral type** instead of the explicit specializations plus the `usize`/`isize` one. It would also give
   `bool` and the character types a hash, changing the documented contract ("specialize it for each key type used") of a main-line container; the
   narrow fix leaves Linux and Windows exactly as they were.
8. **Declined: the `_posix` exemption in `platform_neutral_check` rule 1 is speculative.** The spec (section 5) asks for it with the `posix/`
   split, and Day 306 moves the death-test code into `posix/` files that tests under `tests/debug/` will name; the `.mm` scan is the same rule
   applied to Objective-C++.
