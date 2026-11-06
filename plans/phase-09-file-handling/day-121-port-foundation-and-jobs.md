# Phase 9 / Day 121 — Port 1: Foundation, Jobs and Debugging Startup Code

**Phase:** [Phase 9 — File Handling](../phase-09-file-handling.md)
**Depends on:** Days 116-120.

"Ported" means the old call is gone, not wrapped: the old declaration is deleted (or, for the descriptor primitives, narrowed to its documented purpose), its tests move to the new API, and the old
name no longer builds.

## Tasks

- [x] `os::ReadFileToArena` and `file_util.h/.cpp` removed; callers (`jobs/async_io.cpp`, `tools/docgen/src/source_file.cpp`, `tests/foundation/file_io_test.cpp`) use `File::Open` plus `ReadAll`. The tests keep their cases (ELF magic of `/proc/self/exe`, nonexistent file)
      and gain the new statuses.
- [x] `jobs::ReadFileAsync`/`FileReadResult`/`ReadFileAwaitable` (the public coroutine API) keep their signatures but are implemented only on `File` and the executor, with the file-size probe (`os::Open`+`Lseek`) removed; `jobs::IoThread` stays the executor. The
      `jobs` tests (`async_io_test`) pass unchanged except where they named the old helper.
- [x] `os::EnsureDirectory`, `EnsurePrivateDirectory` and `GetCrashDumpDirectory` removed in favor of `CreateDirectories`, `CreatePrivateDirectory` and `CrashDumpDirectory`; callers (`debug/crash_handler_sentry.cpp` startup, `program_setup`, the demos' cache setup) updated; the crash
      directory policy tests (`tests/debug/core_dump*`, `crash_report*`) pass unchanged in behavior.
- [x] `os/linux/debug/debugger.cpp` and `crash_context.cpp` read `/proc/self/status` and similar through `File` (they run at startup, not in a signal handler; check each against the `SIGNAL-SAFE` markers and record the check).
- [x] The descriptor primitives of `os.h` (`Open`, `Read`, `Write`, `Close`, `Lseek`, `WriteAllWithTimeout`, `CreateFileForWriting`) are **narrowed**: `Open/Lseek/CreateFileForWriting` removed unless a signal-safe caller still needs them (the crash report writer opens its file before the fault; decide per caller and
      record it); `Read`, `Write`, `Close` and `WriteAllWithTimeout` stay, documented as "descriptor primitives for the console, pipes, sockets and the signal-safe layer; not for files". `os.h` and `docs/guide/platform-os.md` say so.
- [x] `crash_handler_sentry.cpp`: the parts outside `SIGNAL-SAFE` markers (stale report removal, directory preparation) use the io layer; the report writing inside the markers keeps raw descriptors (the documented exception, with the reason in a comment and in the spec's exception list).
- [x] Tests: everything that passed before passes; new tests for the ported helpers where behavior changed (error statuses instead of `nullptr`); the Phase 7 gates (`signal_safety_check`, `debug_backend_lists`, `platform_neutral_check`) still pass; the Phase 7 crash tests run (`--all` for the crash label).
- [x] **Documentation:** `platform-os.md` and `jobs.md` updated for the removed and narrowed functions; the guide's "porting" notes list what was moved; reference regenerated; `docgen lint` zero; `ctest -L docs`.
- [x] Run `scripts/test-phase.sh --affected`, then the asan and tsan flavors; also `ctest -L crash`; quote the lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`. Extra scrutiny: anything that ended up allocating or locking inside a signal-safe region.
- [ ] Update `STATUS.md`, the inventory table (mark the rows done); commit.

**Done:** the foundation, jobs and debugging startup code reach files only through `io` (except the documented signal-safe and console descriptors), the old declarations are gone, and every test and gate that passed before still passes.

## Result

- Removed: `os::ReadFileToArena` and `os/file_util.*`, `os::Lseek`, `os::EnsureDirectory`, `os::EnsurePrivateDirectory`, `os::GetCrashDumpDirectory`. Added `io::CrashDumpDirectory` (with the `Backend:` label the Phase 7 checklist test now finds, the BACKEND.md headings renamed). Kept, documented as descriptor primitives: `os::Read/Write/Close/WriteAllWithTimeout`, plus `os::Open` and `os::CreateFileForWriting` for two named callers.
- Ported to `io`: `jobs::ReadFileAsync` (same signature; opens and reads through `io::File` on the I/O thread), `log::LoadConfig`, the core-pattern read in `core_dump.cpp`, the `/proc` reads of `InitCrashContext` (startup only; no `SIGNAL-SAFE` markers in that file), the crash handler's directory preparation and stale-report reset, `tools/docgen`'s source loader, and the tests (`file_io_test`, the crash directory tests).
- **Per-caller decisions (the signal-safety check):** `crash_handler_sentry.cpp`'s `OnCrashEvent` is between the `SIGNAL-SAFE` markers and opens the report file with `os::CreateFileForWriting`: stays raw. `os/linux/debug/debugger.cpp`'s `/proc/self/status` read is not inside markers but `IsDebuggerAttached` is reached from the assert-failure handler, which can run while the heap is inconsistent, and the file layer allocates (`MakeUnique<LocalFile>`): stays raw (recorded in the spec's inventory as exception E2). Nothing inside a `SIGNAL-SAFE` region changed; `signal_safety_check` passes.
- Behavior notes: `CrashDumpDirectory` keeps the old contract of needing `$XDG_STATE_HOME` or `$HOME` (no password-database fallback, unlike the general `User*Directory` functions); `CreatePrivateDirectory` now creates missing parents with mode 0700 like the function it replaces (the crash tests check the application directory's mode).
- Unity-build fix: `io/linux/file_system.cpp` includes `<cstdio>` itself (it had relied on a neighbour for `::remove`/`::rename`).
- Docs: `platform-os.md` ("Descriptors, not files"), `jobs.md`, `foundation-debugging.md`, `porting-debugging.md`; the spec's inventory rows marked done; baseline 3389/3389.
- Scoped runs (after the review fixes): `ran 1255 of 2897 tests (phases: all; flavors: plain; display: included; crash tests: included)` (all pass, including the crash suite and the Phase 7 gates), `ran 807 of 2897 ... flavors: asan`, `ran 527 of 2897 ... flavors: tsan`; `ctest -L docs` 4/4.

## Post-implementation code review

`/code-review high`: 9 findings, 6 fixed, 3 declined.

Fixed:
1. The stale crash-report reset used `io::WriteFile` (mode 0644) where the old code created the file 0600, so a report outside the private directory could become world-readable: back to `os::CreateFileForWriting` (startup, not the handler; the same mode as the fault handler uses).
2. `ReadFileAsync` sized its arena from the reported size, so a `/proc` file (size 0, content present) or a file that grew failed with `OutOfMemory`, where the old function returned an empty success for size 0: the read falls back to a whole-file read and re-sizes the arena (new test reading `/proc/self/status` through `ReadFileAsync`).
3. `tests/debug/BACKEND-CONFORMANCE.md` still named the removed functions: updated to `io::CrashDumpDirectory` and `io::CreatePrivateDirectory`.
4. The result of `ToNative` for the crash directory was ignored: checked, `DirectoryFailed` on failure.
5. The `CreatePrivateDirectory` comment now says existing parents are left alone and are not checked (the old function did not check them either).
6. Bookkeeping: this section, `STATUS.md`, and the day file's scoped-run line.

Declined: (a) `core_dump.cpp` reading through the allocating layer: `EnablePostMortemDumps` is called only from `InstallCrashHandler` at startup (checked), never from a fault path. (b) `log::LoadConfig` depending on `io`: it runs at startup, only edits the channel table after reading, and a failing `Close` of a file opened read-only is not a practical case; the cycle exists at source level only inside one library. (c) The baseline counts: regenerated by `docgen lint --update-baseline`; the totals are 100% (3389/3389), nothing parked.

## Amendment (Day 124, end-of-phase review)

The `/proc` reads of `InitCrashContext` (`os/linux/debug/crash_context.cpp`) were ported here on the reasoning "startup only". The end-of-phase review found that `DefaultAssertHandler` calls `InitCrashContext` lazily when a program never did, so an assertion that fires inside the allocator would allocate through the file layer. The file is restored to the raw descriptor primitives and `readlink` (as before Phase 9) and listed in the access gate's allow list as exception E2, next to `debugger.cpp`.
