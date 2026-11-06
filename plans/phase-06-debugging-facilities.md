# Phase 6 — Debugging Facilities

**Depends on:** Phase 5 (Test scoping), so this phase's own days re-run only their tests. Uses Phases 0-2 (logging and
assertions, process and file wrappers, threads).
**Goal:** when the engine or a game crashes, fails an assertion, or hits a state that should not happen, the developer
gets a readable stack trace, a core dump or minidump to open in a debugger, and macros that distinguish "abort here"
from "report and carry on". Linux first; every interface is shaped so the Windows (Phase 8) and macOS (Phase 9) ports
add backends, not redesigns.
**Definition of done:** a crash (signal, `ENGINE_ASSERT`, unhandled error) prints a symbolized stack trace and writes a
dump file; an `ENGINE_ENSURE` that fails logs once with a trace and execution continues; a test process that crashes on
purpose (in a child process) produces both, and the test reads the report; all of it is documented and the Linux
backend is covered by tests, with the handler off by default in release builds unless enabled.

Every day of this phase has its own file under `plans/phase-06-debugging-facilities/` (see `CLAUDE.md`, planning
policy). **Planned 2026-11-06; not started** (`CURRENT_PHASE` is already 6, so scoped runs select this phase's tests, of
which there are none until Day 92).

## Days

Each day has its own file of individually checkable sub-tasks, a docs task, an end-of-day `/code-review high` task, and a
concrete Done condition. Day numbers continue from Phase 5 (which ended at Day 90).

91. [Design & Spikes](phase-06-debugging-facilities/day-91-design-and-spikes.md)
92. [Death-Test Helper & the `tests/debug` Suite](phase-06-debugging-facilities/day-92-death-test-helper.md)
93. [Assert, Ensure, Verify & the Handler Chain](phase-06-debugging-facilities/day-93-assert-family.md)
94. [Stack Capture](phase-06-debugging-facilities/day-94-stack-capture.md)
95. [Modules & Symbolization](phase-06-debugging-facilities/day-95-symbolization.md)
96. [What a Crash Report Contains](phase-06-debugging-facilities/day-96-crash-report-content.md)
97. [The Crash Handler](phase-06-debugging-facilities/day-97-crash-handler.md)
98. [Core Dumps](phase-06-debugging-facilities/day-98-core-dumps.md)
99. [The Minidump Writer](phase-06-debugging-facilities/day-99-minidump-writer.md)
100. [Minidump Reader & Dump Tool](phase-06-debugging-facilities/day-100-minidump-reader-and-tools.md)
101. [Wiring the Engine In](phase-06-debugging-facilities/day-101-subsystem-integration.md)
102. [Windows & macOS Readiness](phase-06-debugging-facilities/day-102-windows-macos-readiness.md)
103. [Documentation Sweep, Closing Full Run & Wrap-up](phase-06-debugging-facilities/day-103-docs-and-closing-run.md)

Order reasoning: the death-test helper comes first because every later day tests code that aborts or crashes; the assert
family is next because it is the part of the phase every other phase uses; the capture, symbolization and report pieces
are built and tested one at a time before the signal handler composes them; core dumps and minidumps hang off the
handler; integration with the existing subsystems comes next, when the pieces are stable; the Windows and macOS
readiness work (Day 102) comes after the Linux code it mirrors is final.

## Decisions made while planning (Day 91 confirms or amends them with spike results)

- **Symbolization:** raw addresses and `module+offset` first, always (async-signal-safe); function names from an own ELF
  symbol reader; `file:line` offline through `tools/symbolize` (runs `addr2line`, a developer tool). An own DWARF line
  reader is out of scope (filed in `KNOWN-ISSUES.md` on Day 103).
- **Linux minidump:** an own writer, crashing thread only; other threads are listed by id (their stacks need a ptrace
  helper process, filed as a deferral). The reason: one format and one set of readers across Linux, Windows and macOS.
  `lldb` is installed on the development machine (2026-11-06, 23.1.4, with its minidump plugin), so a dump is verified
  by the in-tree reader and by `lldb` in an automated test that skips where `lldb` is missing; `minidump-stackwalk` is
  an optional second reader.
- **`ENGINE_ASSERT`** stays always on and fatal but ends in `abort()` (not `os::Exit(1)`), through a handler chain that
  tests, the crash handler and a debugger share.
- **Where:** `engine/foundation/debug/` (interfaces), `engine/foundation/os/linux/debug/` (backends), tests in the new
  `tests/debug/` with plain, ASan and TSan flavors (`flavors 6 plain,asan,tsan` in `tests/phases.txt`).

## Platform design (PLAN.md assumption #11)

**Scope (decided 2026-11-06):** this phase implements **Linux only**. It leaves **Windows (Phase 8) and macOS (Phase 9)
ready**: everything that can be built and tested on Linux for them is done (neutral interfaces, a portable minidump
serializer and reader, Mach-O and PE module readers, a backend conformance suite, cross-target syntax checks, per-backend
checklists and a porting guide; Day 102), so those phases write only the OS-call layer. Android, iOS and the consoles
(Phases 17 and 18) are **designed in** (the interfaces do not preclude them and `Unsupported` is a valid answer) and get
no work here. Every interface is shaped so that no backend ever forces a redesign. Each facility is a
platform-neutral header in `engine/foundation/debug/` plus a per-platform implementation chosen in CMake the way
`os.h`'s is (a `FATAL_ERROR` on a platform with none). What each platform will use, written down now so the neutral
interfaces fit it (Day 91 verifies this against each platform's documentation and records it in the spec; Day 103
re-checks it against what was built):

| Facility | Linux (this phase) | Windows (8) | macOS (9) | Android / iOS (17) | Consoles (18) |
|---|---|---|---|---|---|
| Stack capture | frame pointers (x86-64 `rbp`, AArch64 `x29`) | `RtlCaptureStackBackTrace` / `RtlVirtualUnwind` (unwind tables, no frame pointers) | frame pointers (the arm64 ABI requires them) | frame pointers (arm64); `_Unwind_Backtrace` fallback | SDK unwinder |
| Fault interception | signals on an alternate stack | vectored + unhandled exception filter, `SetThreadStackGuarantee` for stack overflow | signals plus a Mach exception port (reliable for `EXC_BAD_ACCESS`) | signals (chained with Android's `debuggerd` handler); iOS signals plus Mach | SDK crash hooks |
| Module list and build id | `/proc/self/maps`, ELF `NT_GNU_BUILD_ID` | `EnumProcessModules`, PE CodeView GUID + age | `dyld` image list, Mach-O `LC_UUID` | as Linux (Android), as macOS (iOS) | SDK |
| Symbols | ELF `.symtab`; offline `file:line` | PDB through DbgHelp (OS library) | Mach-O + dSYM | ELF with build id, stripped on device; dSYM | SDK |
| Dump | own minidump writer | `MiniDumpWriteDump` (OS library), same format | own writer | own writer (in-process) | SDK dump |
| Core-dump equivalent | cores / `systemd-coredump` | WER `LocalDumps` (full minidump) | `/cores`, crash reports | tombstones (Android); none (iOS: reports `Unsupported`) | SDK |
| Child process for death tests | re-exec of the test binary (`posix_spawn`) | `CreateProcess` re-exec | `posix_spawn` | no spawn on iOS: tests skip with a reason; run on desktop CI | skipped |
| Dump directory | `os::` crash-dump directory | same | same | app-private/sandbox container | SDK |

Rules that follow, applied in every day file:

- **Neutral headers.** No public header names `ucontext_t`, a signal number, `pid_t` or `/proc`. A crash is described by
  a `CrashReason` enum (access violation, bus error, illegal instruction, arithmetic fault, stack overflow, abort,
  assert, ensure) plus the raw platform code; the interrupted CPU state travels as an opaque `NativeContext`; thread ids
  are `u64`; output goes through a small sink type, not a hard-wired descriptor. File and process access reuse the
  `os::` seam.
- **Two architectures from the start.** The context record, the register walker and the minidump `CONTEXT` exist for
  x86-64 and AArch64 (Apple silicon, Android, iOS, Windows on ARM). The AArch64 code compiles in CI here by
  cross-targeting `clang --target=aarch64-linux-gnu -fsyntax-only` where the headers allow, and is otherwise listed in
  `KNOWN-ISSUES.md` as not run.
- **Portable core, thin platform snapshot.** The minidump writer, reader, report formatter, ring buffer and assert
  machinery contain no OS calls. A platform backend fills a `CrashSnapshot` (threads, contexts, modules, memory
  ranges) and the portable writer serializes it, so it is tested here with synthetic Windows-, macOS- and
  Android-shaped snapshots.
- **No assumption of a process model, a filesystem layout or a command line.** Mobile has no command line and a sandbox;
  crash options come from the engine config as well as flags; dumps go to an `os::`-provided directory; features a
  platform cannot offer return `Unsupported` and are reported, not silently skipped.
- **Compiler differences stay in one header** (`debug/compiler.h`: debug break, unreachable, noinline, format
  attributes) so a Windows build with clang-cl or MSVC changes one file.

## Key deliverables

- **Stack traces.** Capture the call stack of the current thread (and, from a signal handler, the faulting thread)
  without allocating; symbolize it into function, file and line when debug info is present, and into module plus
  offset when it is not. Linux: frame-pointer or unwind-table walking and symbol lookup through the system's ELF/DWARF
  data (`dladdr`, `.eh_frame`; a small own DWARF line reader, or a call to `addr2line` offline). Output formats: text for
  the log, a structured form for dump files. No third-party symbolization library (zero-dependency rule; see the open
  question).
- **Crash handler.** Installs handlers for fatal signals (SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL) on an alternate
  stack, prints the trace and the engine's state summary (build id, platform, GPU, last log lines from a ring buffer),
  writes the dump, then re-raises so the OS (and any debugger) still sees the original fault. Async-signal-safe by
  construction: no heap use, no locks, no stdio in the handler.
- **Core dumps.** Make them happen reliably: `setrlimit(RLIMIT_CORE)` at startup, documented `core_pattern` and
  `systemd-coredump` guidance for developers, a script that finds the latest core, matches it with the build's
  symbols and opens it in gdb.
- **Minidumps.** One portable dump format written by the engine itself: the Microsoft minidump format (threads,
  registers, stack memory, module list, exception record), so the same file is read by WinDbg, Visual Studio, lldb and
  `minidump-stackwalk`. Windows writes through `MiniDumpWriteDump` (an OS library, allowed) in Phase 8; Linux writes
  the same format with an own writer from the crash handler. Decision to settle in the day files: own writer on Linux
  versus gdb/core only.
- **Assert, ensure and verify macros.** Extend `ENGINE_ASSERT` (aborts, always on in debug, kept in release for the
  documented contract violations) with `ENGINE_ENSURE` (reports once per site with a trace, returns the condition so
  the caller can recover; counted), `ENGINE_VERIFY` (the expression is always evaluated, checked only in debug),
  `ENGINE_CHECK_SLOW` (compiled out outside debug), and `ENGINE_UNREACHABLE`/`ENGINE_NOT_IMPLEMENTED`. A pluggable
  handler (`SetAssertHandler`) lets tests capture failures instead of aborting, and a debugger-break hook stops at the
  failing line when a debugger is attached. Every message carries file, line, function and the stringified condition.
- **Death-test support.** A helper that runs a function in a forked child and returns its exit status, signal and
  captured stderr, so crash and assert paths are tested without killing the test runner.
- **Integration.** The logger stamps a build id; the RHI's device-lost and validation errors attach a trace; the job
  system's worker threads install the same handlers; `Fails:`/`Aborts:` docs (see the Day 83 classification) reference
  these macros.
- **Closing run.** The phase's last day runs `scripts/test-phase.sh --all --flavor all` (every test in plain, ASan and
  TSan), `ctest -L docs` and the demos on both display backends, and records the `ran X of Y tests` line in its day
  file (Phase 5 policy; day-to-day runs are scoped to the current phase).

## Notes / open questions

- **Symbolization without a third-party library.** `dladdr` gives function names only for exported symbols; line numbers
  need DWARF. Options: write a small DWARF line-table reader (a real chunk of work), resolve offline with `addr2line` at
  report time (the dump stores module + offset), or accept function-level traces in the first pass. Proposed: offsets in
  the dump plus a developer tool that resolves them, and function names at crash time.
- **Does the own minidump writer on Linux earn its cost?** It buys one tool path across platforms; core dumps already
  work with gdb. Settle it in the day files before any code is written.
- ASan/TSan install their own handlers; the crash handler must cooperate (chain to the previous handler) and be
  switchable off in sanitizer builds.
- Wayland/X11 and the GPU driver run their own threads; a fault inside a driver should still produce a trace of the
  faulting thread.
- Privacy and size: dumps may contain memory of the process; document where they are written and that they are never
  uploaded by the engine.
