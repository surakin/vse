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

Before it starts, every day of this phase needs its own file under `plans/phase-06-debugging-facilities/` (see
`CLAUDE.md`, planning policy).

## Key deliverables (to be expanded into day-by-day tasks when started)

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
