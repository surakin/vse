# Phase 6 / Day 96 — What a Crash Report Contains

**Phase:** [Phase 6 — Debugging Facilities](../phase-06-debugging-facilities.md)
**Depends on:** Day 95.

The handler of Day 97 must only read state that is already prepared. Build that state now: the build id stamp, a
platform summary, a ring buffer of the last log lines, and a registry for "engine state" providers (GPU name, current
frame, scene). Everything is filled during normal running and read, never allocated, in the handler.

## Tasks

- [ ] Add `engine/foundation/debug/crash_context.h`: `CrashContext` (a process-wide object in static storage, constant-initialized, with fixed buffers): build id string, executable path, command line (truncated to 1 KB), OS name and version, architecture, device model (mobile and consoles; empty on desktop), CPU brand and count, total memory (each filled by a small per-platform function; Linux uses `uname` and `/proc`), engine version string, start time. `InitCrashContext(const CommandLine*)` (the command line is optional: mobile and console programs have none) fills it once at startup (this may allocate and call anything; it runs before any handler exists).
- [ ] Log ring buffer: the logger (`engine/foundation/log/log.cpp`) copies each emitted line (after formatting, truncated to 256 bytes) into a lock-free ring of 256 slots (a fetch-add on a head index, each slot guarded by a sequence number so a reader can detect a torn slot), in addition to the existing write sink. The writer path takes no lock and allocates nothing; the cost per log line is measured (target: under 100 ns on top of the existing formatting) and recorded. `ReadRecentLogLines(callback)` is async-signal-safe and skips torn slots.
- [ ] State providers: `RegisterCrashStateProvider(name, fn, ctx)` (fixed table of 16 entries, append-only, registered at startup) where `fn(ctx, char* out, usize cap)` must itself be signal-safe and read only plain memory; document the rule loudly. First users (later days wire them): the RHI adds the adapter name and driver version; the renderer adds the frame index; the platform layer adds the window backend.
- [ ] The ring buffer, the context and the formatter use only the C++ atomics and `os::` calls; they contain no platform code and are the same on every platform. The per-thread name `thread_local` is likewise portable.
- [ ] The report formatter `WriteCrashReport(fd, const CrashReason&, const StackTrace&)` with the order fixed on Day 91: header line with the reason (`SIGSEGV at address 0x...`, `ASSERT FAILED ...`, `ENSURE failed ...`), thread id and name (`pthread_getname_np` is not safe in a signal; keep each thread's name in a `thread_local` char array set by the job system and the main thread at startup), build id and platform, the stack trace, state providers, and the last log lines (oldest first). It writes in small `write(2)` calls so a partial report survives a second fault.
- [ ] Wire `ENGINE_ASSERT` and a failing `ENGINE_ENSURE` to print the same report body (without the signal-specific lines), so an assert and a crash look alike; test with the capture handler.
- [ ] Tests (in `tests/debug`): the context holds the build id the binary has; the ring returns the last 256 lines in order after 1000 and after 10 lines, and after 8 threads log concurrently (TSan: no race; every line read is whole or skipped); a provider that writes more than its capacity is truncated; the report for a synthetic reason contains each section in order (parse the text); the report writer allocates nothing (counting allocator in a child) and survives a second fault in the middle (child: provider that crashes; the report written so far is intact and the process still terminates).
- [ ] **Documentation:** `///` comments; the guide gets "What a crash report contains" with a sample report and the provider rules; reference regenerated and committed; `docgen lint` at zero; `ctest -L docs` passes.
- [ ] Run the day's scoped tests (`--affected`, then asan and tsan flavors); quote the `ran X of Y tests` lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high` on the day's diff; extra attention to the ring buffer's memory ordering and to anything the report reads that another thread may be writing; record it here and in `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** the logger feeds a lock-free ring of recent lines, the crash context and providers exist, the report formatter writes the fixed layout without allocating, assert and ensure print it, and the tests pass in plain, ASan and TSan with the per-log-line cost recorded.
