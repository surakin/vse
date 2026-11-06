# Phase 6 / Day 96 — What a Crash Report Contains

**Phase:** [Phase 6 — Debugging Facilities](../phase-06-debugging-facilities.md)
**Depends on:** Days 94 and 95.

The crash library writes the minidump (every thread, registers, modules, stack memory). What it cannot know is the
engine's own state. Prepare that state during normal running so a crash only has to read it: the build id, a platform
summary, a ring buffer of the last log lines, and a registry of "engine state" providers (GPU name, current frame, window
backend). Everything is filled ahead of time and read, never allocated, in the crash path.

## Tasks

- [ ] Add `engine/foundation/debug/crash_context.h`: `CrashContext` (process-wide, static storage, constant-initialized, fixed buffers): build id string (the executable's `NT_GNU_BUILD_ID` note read once at startup with the `PT_NOTE` of `/proc/self/exe`, or the linker-embedded id; a small function per platform), executable path, command line (truncated to 1 KB, optional: mobile and console programs have none), OS name and version, architecture, device model (empty on desktop), CPU brand and count, total memory, engine version, start time. `InitCrashContext(const CommandLine*)` fills it once at startup (it may allocate; no handler exists yet).
- [ ] Log ring buffer: the logger (`engine/foundation/log/log.cpp`) copies each emitted line (formatted, truncated to 256 bytes) into a lock-free ring of 256 slots (a fetch-add on a head index, each slot guarded by a sequence number so a reader detects a torn slot), in addition to the existing write sink. The writer takes no lock and allocates nothing; the added cost per log line is measured (target under 100 ns) and recorded. `ReadRecentLogLines(callback)` is allocation-free and skips torn slots.
- [ ] State providers: `RegisterCrashStateProvider(name, fn, ctx)` (fixed table of 16, append-only, registered at startup); `fn(ctx, char* out, usize cap)` must read only plain memory and be allocation-free; document the rule loudly. The RHI, renderer and platform layer register theirs on Day 100.
- [ ] The report: `WriteCrashReport(sink, const CrashReason&, const StackTrace&)` writes, in a fixed order, the reason (`ACCESS_VIOLATION at 0x...`, `ASSERT FAILED ...`), the thread id and name (names kept by the job system and main thread in a `thread_local` char array), the build id and platform summary, the resolved trace (Day 94), the state providers, and the last log lines oldest first. It writes in small chunks so a partial report survives a second fault. It is called from the library's `on_crash` hook (Day 91 Spike D: the hook runs in the crashing process on every backend, so no signal handler of ours is needed) and writes to the report sink and to the file that was attached to the library at install time (Day 95).
- [ ] Assert and ensure print the same report body (no fault lines) so an assert and a crash look alike; test with the Day 93 capture handler.
- [ ] The library's own dump gets the context too, as Day 91 decided: the build id, platform summary and provider text as the library's release string (sentry-native: `sentry_options_set_release`, set to the build id at install) and the attached report file; test that the extracted dump or the envelope contains the build id.
- [ ] Tests (in `tests/debug`): the context holds the build id the binary has (`readelf -n` agrees); the ring returns the last 256 lines in order after 1000 and after 10 lines and after 8 threads log concurrently (TSan: no race; every line is whole or skipped); a provider that writes more than its capacity is truncated; the report for a synthetic reason has every section in order (parse the text); the report writer allocates nothing (counting allocator in a child) and survives a second fault in the middle (a provider that crashes: the report written so far is intact and the process still terminates).
- [ ] **Documentation:** `///` comments; the guide gets "What a crash report contains" with a sample report and the provider rules; reference regenerated and committed; `docgen lint` at zero; `ctest -L docs` passes.
- [ ] Run the day's scoped tests (`--affected`, then asan and tsan flavors); quote the `ran X of Y tests` lines.
- [ ] **End-of-day review (CLAUDE.md):** `/code-review high`; extra attention to the ring buffer's memory ordering and to anything the report reads that another thread may be writing; record it here and in `STATUS.md`.
- [ ] Update `STATUS.md`; commit.

**Done:** the logger feeds a lock-free ring of recent lines with its per-line cost recorded, the crash context and providers exist, the report writer produces the fixed layout without allocating, assert and ensure print it, the dump carries the build id, and the tests pass in plain, ASan and TSan.
