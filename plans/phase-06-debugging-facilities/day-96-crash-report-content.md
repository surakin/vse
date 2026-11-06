# Phase 6 / Day 96 — What a Crash Report Contains

**Phase:** [Phase 6 — Debugging Facilities](../phase-06-debugging-facilities.md)
**Depends on:** Days 94 and 95.

The crash library writes the minidump (every thread, registers, modules, stack memory). What it cannot know is the
engine's own state. Prepare that state during normal running so a crash only has to read it: the build id, a platform
summary, a ring buffer of the last log lines, and a registry of "engine state" providers (GPU name, current frame, window
backend). Everything is filled ahead of time and read, never allocated, in the crash path.

## Tasks

- [x] Add `engine/foundation/debug/crash_context.h`: `CrashContext` (process-wide, static storage, constant-initialized, fixed buffers): build id string (the executable's `NT_GNU_BUILD_ID` note read once at startup with the `PT_NOTE` of `/proc/self/exe`, or the linker-embedded id; a small function per platform), executable path, command line (truncated to 1 KB, optional: mobile and console programs have none), OS name and version, architecture, device model (empty on desktop), CPU brand and count, total memory, engine version, start time. `InitCrashContext(const CommandLine*)` fills it once at startup (it may allocate; no handler exists yet).
- [x] Log ring buffer: the logger (`engine/foundation/log/log.cpp`) copies each emitted line (formatted, truncated to 256 bytes) into a lock-free ring of 256 slots (a fetch-add on a head index, each slot guarded by a sequence number so a reader detects a torn slot), in addition to the existing write sink. The writer takes no lock and allocates nothing; the added cost per log line is measured (target under 100 ns) and recorded. `ReadRecentLogLines(callback)` is allocation-free and skips torn slots.
- [x] State providers: `RegisterCrashStateProvider(name, fn, ctx)` (fixed table of 16, append-only, registered at startup); `fn(ctx, char* out, usize cap)` must read only plain memory and be allocation-free; document the rule loudly. The RHI, renderer and platform layer register theirs on Day 100.
- [x] The report: `WriteCrashReport(sink, const CrashReason&, const StackTrace&)` writes, in a fixed order, the reason (`ACCESS_VIOLATION at 0x...`, `ASSERT FAILED ...`), the thread id and name (names kept by the job system and main thread in a `thread_local` char array), the build id and platform summary, the resolved trace (Day 94), the state providers, and the last log lines oldest first. It writes in small chunks so a partial report survives a second fault. It is called from the library's `on_crash` hook (Day 91 Spike D: the hook runs in the crashing process on every backend, so no signal handler of ours is needed) and writes to the report sink and to the file that was attached to the library at install time (Day 95).
- [x] Assert and ensure print the same report body (no fault lines) so an assert and a crash look alike; test with the Day 93 capture handler.
- [x] The library's own dump gets the context too, as Day 91 decided: the build id, platform summary and provider text as the library's release string (sentry-native: `sentry_options_set_release`, set to the build id at install) and the attached report file; test that the extracted dump or the envelope contains the build id.
- [x] Tests (in `tests/debug`): the context holds the build id the binary has (`readelf -n` agrees); the ring returns the last 256 lines in order after 1000 and after 10 lines and after 8 threads log concurrently (TSan: no race; every line is whole or skipped); a provider that writes more than its capacity is truncated; the report for a synthetic reason has every section in order (parse the text); the report writer allocates nothing (counting allocator in a child) and survives a second fault in the middle (a provider that crashes: the report written so far is intact and the process still terminates).
- [x] **Documentation:** `///` comments; the guide gets "What a crash report contains" with a sample report and the provider rules; reference regenerated and committed; `docgen lint` at zero; `ctest -L docs` passes.
- [x] Run the day's scoped tests (`--affected`, then asan and tsan flavors); quote the `ran X of Y tests` lines.
- [x] **End-of-day review (CLAUDE.md):** `/code-review high`; extra attention to the ring buffer's memory ordering and to anything the report reads that another thread may be writing; record it here and in `STATUS.md`.
- [x] Update `STATUS.md`; commit.

**Done:** the logger feeds a lock-free ring of recent lines with its per-line cost recorded, the crash context and providers exist, the report writer produces the fixed layout without allocating, assert and ensure print it, the dump carries the build id, and the tests pass in plain, ASan and TSan.


## Result

Phase 6 has 346 tests (63 new this day). Scoped runs: `ran 997 of 2218 tests (phases: all; flavors: plain; display: included)`
(one known display failure, the X11 window test), `ran 743 of 2218 ... asan`, `ran 478 of 2218 ... tsan`. Also green in the
non-unity, clang and Release builds, Release with forced checks, 5 repeats of the phase under `-j16`, and `ctest -L docs`;
`docgen lint` at 2656/2656. Mutation checks: ignoring the ring's sequence check fails the 8-thread test; renaming a report
header fails four tests.

Measured: one `Log` call costs 33-48 ns in Release including the ring append (loop of 2,000,000 lines to a discarding sink),
under the 100 ns target.

Pages touched: `docs/guide/foundation-debugging.md` ("What a crash report contains"), `docs/reference/foundation-debug.md`,
`docs/reference/foundation-log.md`.

Note for Day 97: `WriteCrashReport` keeps an 8 KiB trace buffer on the stack; check it fits the alternate signal stack the
Breakpad handler runs on when the report is called from the `on_crash` hook.

## Post-implementation code review

`/code-review high` found 9 issues.

1. `cpu_brand` always empty: `/proc/cpuinfo` has a tab before the colon. **Fixed**; test reads `/proc/cpuinfo` and requires a brand.
2. Provider table published the count before filling the slot. **Fixed**: per-entry `ready` flag set with release, reports skip entries not ready.
3. Ring writer's acquire exchange does not order later stores on weakly ordered CPUs. **Fixed**: the data stores are release stores (a fence would not be seen by TSan).
4. Command line lost if the crash handler initialized the context first; assert before init printed empty sections. **Fixed**: a later `InitCrashContext` with arguments fills an empty command line; the assert path initializes the context; test added.
5. `AssertReportWrite` had external linkage. **Fixed** (`static`).
6. Three copies of a bounded string copy. **Declined**: three four-line loops in different layers; a shared header costs more than it saves.
7. Build-id note parser did not bound the name or guard wraparound. **Fixed**.
8. Last trace line without a newline dropped. **Fixed** (remainder is written). The 8 KiB stack buffer is **recorded for Day 97**, not changed.
9. A provider that asserts recurses through the assert handler. **Fixed**: a per-thread guard skips providers on re-entry; test added.

The fixes also exposed that the 8-thread test's at-rest count of exactly 256 was too strict (a writer descheduled mid-slot and lapped leaves one slot skipped); the test now allows a few missing slots while still requiring every delivered line whole.
