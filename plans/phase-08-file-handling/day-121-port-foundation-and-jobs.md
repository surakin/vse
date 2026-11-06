# Phase 8 / Day 121 — Port 1: Foundation, Jobs and Debugging Startup Code

**Phase:** [Phase 8 — File Handling](../phase-08-file-handling.md)
**Depends on:** Days 116-120.

"Ported" means the old call is gone, not wrapped: the old declaration is deleted (or, for the descriptor primitives, narrowed to its documented purpose), its tests move to the new API, and the old
name no longer builds.

## Tasks

- [ ] `os::ReadFileToArena` and `file_util.h/.cpp` removed; callers (`jobs/async_io.cpp`, `tools/docgen/src/source_file.cpp`, `tests/foundation/file_io_test.cpp`) use `File::Open` plus `ReadAll`. The tests keep their cases (ELF magic of `/proc/self/exe`, nonexistent file)
      and gain the new statuses.
- [ ] `jobs::ReadFileAsync`/`FileReadResult`/`ReadFileAwaitable` (the public coroutine API) keep their signatures but are implemented only on `File` and the executor, with the file-size probe (`os::Open`+`Lseek`) removed; `jobs::IoThread` stays the executor. The
      `jobs` tests (`async_io_test`) pass unchanged except where they named the old helper.
- [ ] `os::EnsureDirectory`, `EnsurePrivateDirectory` and `GetCrashDumpDirectory` removed in favor of `CreateDirectories`, `CreatePrivateDirectory` and `CrashDumpDirectory`; callers (`debug/crash_handler_sentry.cpp` startup, `program_setup`, the demos' cache setup) updated; the crash
      directory policy tests (`tests/debug/core_dump*`, `crash_report*`) pass unchanged in behavior.
- [ ] `os/linux/debug/debugger.cpp` and `crash_context.cpp` read `/proc/self/status` and similar through `File` (they run at startup, not in a signal handler; check each against the `SIGNAL-SAFE` markers and record the check).
- [ ] The descriptor primitives of `os.h` (`Open`, `Read`, `Write`, `Close`, `Lseek`, `WriteAllWithTimeout`, `CreateFileForWriting`) are **narrowed**: `Open/Lseek/CreateFileForWriting` removed unless a signal-safe caller still needs them (the crash report writer opens its file before the fault; decide per caller and
      record it); `Read`, `Write`, `Close` and `WriteAllWithTimeout` stay, documented as "descriptor primitives for the console, pipes, sockets and the signal-safe layer; not for files". `os.h` and `docs/guide/platform-os.md` say so.
- [ ] `crash_handler_sentry.cpp`: the parts outside `SIGNAL-SAFE` markers (stale report removal, directory preparation) use the io layer; the report writing inside the markers keeps raw descriptors (the documented exception, with the reason in a comment and in the spec's exception list).
- [ ] Tests: everything that passed before passes; new tests for the ported helpers where behavior changed (error statuses instead of `nullptr`); the Phase 6 gates (`signal_safety_check`, `debug_backend_lists`, `platform_neutral_check`) still pass; the Phase 6 crash tests run (`--all` for the crash label).
- [ ] **Documentation:** `platform-os.md` and `jobs.md` updated for the removed and narrowed functions; the guide's "porting" notes list what was moved; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [ ] Run `scripts/test-phase.sh --affected`, then the asan and tsan flavors; also `ctest -L crash`; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`. Extra scrutiny: anything that ended up allocating or locking inside a signal-safe region.
- [ ] Update `STATUS.md`, the inventory table (mark the rows done); commit.

**Done:** the foundation, jobs and debugging startup code reach files only through `io` (except the documented signal-safe and console descriptors), the old declarations are gone, and every test and gate that passed before still passes.
